using PrismaApi.Domain.Dtos;
using PrismaApi.Domain.Extensions;

namespace PrismaApi.Test.UnitTests;

/// <summary>
/// <see cref="InfluenceDiagramDtoExtensions.ApplyRestrictions"/> ends by normalizing the
/// probability rows that a restriction entry may have reduced. A row is one uncertainty under
/// one configuration of its parents; rows of different uncertainties that happen to share a
/// parent configuration (two uncertainties with the same parent, or several root
/// uncertainties) must never be normalized together. These tests pin that, plus the existing
/// behavior for a single row and for a restricted cell, through the public entry point.
/// </summary>
public class InfluenceDiagramDtoExtensionsTests
{
    private const double Tolerance = 1e-12;
    private static readonly Guid ProjectId = Guid.NewGuid();

    private static DiscreteProbabilityDto Cell(
        Guid uncertaintyId,
        Guid outcomeId,
        double probability,
        IEnumerable<Guid>? parentOutcomeIds = null,
        IEnumerable<Guid>? parentOptionIds = null) =>
        new()
        {
            ProjectId = ProjectId,
            UncertaintyId = uncertaintyId,
            OutcomeId = outcomeId,
            Probability = probability,
            ParentOutcomeIds = parentOutcomeIds?.ToList() ?? new List<Guid>(),
            ParentOptionIds = parentOptionIds?.ToList() ?? new List<Guid>(),
        };

    private static InfluenceDiagramDto Diagram(
        IEnumerable<DiscreteProbabilityDto> cells,
        IEnumerable<RestrictionTableOutgoingDto>? restrictionTables = null) =>
        new()
        {
            projectId = ProjectId,
            issues = new List<IssueOutgoingDto>(),
            edges = new List<EdgeOutgoingDto>(),
            discreteProbabilities = cells.ToList(),
            discreteUtilities = new List<DiscreteUtilityDto>(),
            restrictionTables = restrictionTables?.ToList() ?? new List<RestrictionTableOutgoingDto>(),
        };

    /// <summary>Sum of the cells of one uncertainty under one parent configuration.</summary>
    private static double RowSum(InfluenceDiagramDto diagram, Guid uncertaintyId, params Guid[] parentStateIds)
    {
        var wanted = parentStateIds.OrderBy(x => x).ToList();
        return diagram.discreteProbabilities
            .Where(dp => dp.UncertaintyId == uncertaintyId
                         && dp.ParentOutcomeIds.Concat(dp.ParentOptionIds).OrderBy(x => x).SequenceEqual(wanted))
            .Sum(dp => dp.Probability ?? 0.0);
    }

    private static double CellValue(InfluenceDiagramDto diagram, Guid outcomeId, params Guid[] parentStateIds)
    {
        var wanted = parentStateIds.OrderBy(x => x).ToList();
        return diagram.discreteProbabilities
            .Single(dp => dp.OutcomeId == outcomeId
                          && dp.ParentOutcomeIds.Concat(dp.ParentOptionIds).OrderBy(x => x).SequenceEqual(wanted))
            .Probability!.Value;
    }

    private static (InfluenceDiagramDto Diagram, Guid W0, Guid W1, Guid A0, Guid A1, Guid B0, Guid B1) SharedParentModel()
    {
        // W (two outcomes) is the parent of both A and B; every row sums to 1.
        var w = Guid.NewGuid(); var w0 = Guid.NewGuid(); var w1 = Guid.NewGuid();
        var a = Guid.NewGuid(); var a0 = Guid.NewGuid(); var a1 = Guid.NewGuid();
        var b = Guid.NewGuid(); var b0 = Guid.NewGuid(); var b1 = Guid.NewGuid();
        var cells = new List<DiscreteProbabilityDto>
        {
            Cell(w, w0, 0.25), Cell(w, w1, 0.75),
            Cell(a, a0, 0.6, new[] { w0 }), Cell(a, a1, 0.4, new[] { w0 }),
            Cell(a, a0, 0.1, new[] { w1 }), Cell(a, a1, 0.9, new[] { w1 }),
            Cell(b, b0, 0.5, new[] { w0 }), Cell(b, b1, 0.5, new[] { w0 }),
            Cell(b, b0, 0.2, new[] { w1 }), Cell(b, b1, 0.8, new[] { w1 }),
        };
        return (Diagram(cells), w0, w1, a0, a1, b0, b1);
    }

    [Fact]
    public void N1_TwoUncertaintiesSharingAParent_AreNormalizedSeparately()
    {
        var (diagram, w0, w1, a0, a1, b0, b1) = SharedParentModel();
        var before = diagram.discreteProbabilities.Select(dp => (dp.Id, dp.Probability)).ToList();

        diagram.ApplyRestrictions();

        foreach (var (id, probability) in before)
        {
            var after = diagram.discreteProbabilities.Single(dp => dp.Id == id).Probability;
            Assert.Equal(probability!.Value, after!.Value, Tolerance);
        }
        Assert.Equal(0.6, CellValue(diagram, a0, w0), Tolerance);
        Assert.Equal(0.4, CellValue(diagram, a1, w0), Tolerance);
        Assert.Equal(0.1, CellValue(diagram, a0, w1), Tolerance);
        Assert.Equal(0.9, CellValue(diagram, a1, w1), Tolerance);
        Assert.Equal(0.5, CellValue(diagram, b0, w0), Tolerance);
        Assert.Equal(0.5, CellValue(diagram, b1, w0), Tolerance);
        Assert.Equal(0.2, CellValue(diagram, b0, w1), Tolerance);
        Assert.Equal(0.8, CellValue(diagram, b1, w1), Tolerance);
    }

    [Fact]
    public void N2_SeveralRootUncertainties_KeepTheirPriors()
    {
        var roots = Enumerable.Range(0, 3).Select(_ => (Id: Guid.NewGuid(), S0: Guid.NewGuid(), S1: Guid.NewGuid())).ToList();
        var priors = new[] { (0.4, 0.6), (0.5, 0.5), (0.1, 0.9) };
        var cells = roots.SelectMany((r, i) => new[]
        {
            Cell(r.Id, r.S0, priors[i].Item1),
            Cell(r.Id, r.S1, priors[i].Item2),
        }).ToList();
        var diagram = Diagram(cells);

        diagram.ApplyRestrictions();

        for (var i = 0; i < roots.Count; i++)
        {
            Assert.Equal(priors[i].Item1, CellValue(diagram, roots[i].S0), Tolerance);
            Assert.Equal(priors[i].Item2, CellValue(diagram, roots[i].S1), Tolerance);
            Assert.Equal(1.0, RowSum(diagram, roots[i].Id), Tolerance);
        }
    }

    private static (InfluenceDiagramDto Diagram, Guid D, Guid A, Guid A0, Guid A1, Guid A2, Guid B0, Guid B1, Guid TableId, Guid EdgeId)
        RestrictedSiblingModel()
    {
        // A (three outcomes) and B (two) are both conditioned on option d of a decision.
        // One restriction entry forbids A's third outcome under d.
        var d = Guid.NewGuid();
        var a = Guid.NewGuid(); var a0 = Guid.NewGuid(); var a1 = Guid.NewGuid(); var a2 = Guid.NewGuid();
        var b = Guid.NewGuid(); var b0 = Guid.NewGuid(); var b1 = Guid.NewGuid();
        var cells = new List<DiscreteProbabilityDto>
        {
            Cell(a, a0, 0.5, parentOptionIds: new[] { d }),
            Cell(a, a1, 0.3, parentOptionIds: new[] { d }),
            Cell(a, a2, 0.2, parentOptionIds: new[] { d }),
            Cell(b, b0, 0.4, parentOptionIds: new[] { d }),
            Cell(b, b1, 0.6, parentOptionIds: new[] { d }),
        };
        var tableId = Guid.NewGuid();
        var edgeId = Guid.NewGuid();
        var table = new RestrictionTableOutgoingDto
        {
            Id = tableId,
            ProjectId = ProjectId,
            EdgeId = edgeId,
            RestrictionEntries = new List<RestrictionEntryOutgoingDto>
            {
                new()
                {
                    ProjectId = ProjectId,
                    RestrictionTableId = tableId,
                    RestrictionValue = 0,
                    ParentStateId = d,
                    IsParentUncertainty = false,
                    ChildStateId = a2,
                    IsChildUncertainty = true,
                },
            },
        };
        return (Diagram(cells, new[] { table }), d, a, a0, a1, a2, b0, b1, tableId, edgeId);
    }

    [Fact]
    public void N3_ARestrictedCell_RenormalizesItsOwnRowOnly()
    {
        var (diagram, d, a, a0, a1, a2, b0, b1, _, _) = RestrictedSiblingModel();

        diagram.ApplyRestrictions();

        Assert.Equal(0.625, CellValue(diagram, a0, d), Tolerance);
        Assert.Equal(0.375, CellValue(diagram, a1, d), Tolerance);
        Assert.Equal(0.0, CellValue(diagram, a2, d), Tolerance);
        Assert.Equal(1.0, RowSum(diagram, a, d), Tolerance);
        Assert.Equal(0.4, CellValue(diagram, b0, d), Tolerance);
        Assert.Equal(0.6, CellValue(diagram, b1, d), Tolerance);
    }

    [Fact]
    public void N4_ASingleRowOffByMoreThanTheTolerance_IsNormalizedAsBefore()
    {
        var u = Guid.NewGuid(); var u0 = Guid.NewGuid(); var u1 = Guid.NewGuid();
        var diagram = Diagram(new[] { Cell(u, u0, 0.5), Cell(u, u1, 0.3) });

        diagram.ApplyRestrictions();

        Assert.Equal(0.625, CellValue(diagram, u0), Tolerance);
        Assert.Equal(0.375, CellValue(diagram, u1), Tolerance);
    }

    [Fact]
    public void N5_TheResult_DoesNotDependOnTheOrderOfTheCells()
    {
        var (reference, w0, w1, a0, a1, b0, b1) = SharedParentModel();
        reference.ApplyRestrictions();
        var expected = reference.discreteProbabilities
            .ToDictionary(dp => (dp.OutcomeId, Key(dp)), dp => dp.Probability!.Value);

        foreach (var order in new[] { "reversed", "shuffled" })
        {
            var (diagram, _, _, _, _, _, _) = SharedParentModel();
            var cells = diagram.discreteProbabilities.ToList();
            // Rebuild with the same values under fresh ids in a different order.
            var reordered = order == "reversed" ? Enumerable.Reverse(cells).ToList() : Shuffle(cells, seed: 7);
            var copy = Diagram(reordered.Select(dp => Cell(dp.UncertaintyId, dp.OutcomeId, dp.Probability!.Value, dp.ParentOutcomeIds, dp.ParentOptionIds)));
            copy.ApplyRestrictions();
            foreach (var dp in copy.discreteProbabilities)
            {
                // Same structure, fresh ids: compare by position in the model, i.e. by value pattern.
                Assert.Equal(1.0, RowSum(copy, dp.UncertaintyId, dp.ParentOutcomeIds.Concat(dp.ParentOptionIds).ToArray()), Tolerance);
            }
            var original = diagram.discreteProbabilities.ToDictionary(dp => (dp.OutcomeId, Key(dp)), dp => dp.Probability!.Value);
            foreach (var dp in copy.discreteProbabilities)
            {
                Assert.Equal(original[(dp.OutcomeId, Key(dp))], dp.Probability!.Value, Tolerance);
            }
        }

        var (restricted, d, a, ra0, ra1, ra2, rb0, rb1, _, _) = RestrictedSiblingModel();
        var reversedCells = Enumerable.Reverse(restricted.discreteProbabilities.ToList())
            .Select(dp => Cell(dp.UncertaintyId, dp.OutcomeId, dp.Probability!.Value, dp.ParentOutcomeIds, dp.ParentOptionIds));
        var restrictedReversed = Diagram(reversedCells, restricted.restrictionTables);
        restrictedReversed.ApplyRestrictions();
        Assert.Equal(0.625, CellValue(restrictedReversed, ra0, d), Tolerance);
        Assert.Equal(0.375, CellValue(restrictedReversed, ra1, d), Tolerance);
        Assert.Equal(0.0, CellValue(restrictedReversed, ra2, d), Tolerance);
        Assert.Equal(0.4, CellValue(restrictedReversed, rb0, d), Tolerance);
        Assert.Equal(0.6, CellValue(restrictedReversed, rb1, d), Tolerance);
        _ = expected;
    }

    private static string Key(DiscreteProbabilityDto dp) =>
        string.Join(",", dp.ParentOutcomeIds.Concat(dp.ParentOptionIds).OrderBy(x => x));

    private static List<DiscreteProbabilityDto> Shuffle(List<DiscreteProbabilityDto> cells, int seed)
    {
        var random = new Random(seed);
        var copy = cells.ToList();
        for (var i = copy.Count - 1; i > 0; i--)
        {
            var j = random.Next(i + 1);
            (copy[i], copy[j]) = (copy[j], copy[i]);
        }
        return copy;
    }
}
