using System.Net;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using PrismaApi.Application.Interfaces.Services;
using PrismaApi.Domain.Dtos;
using PrismaApi.Test.Configuration;
using PrismaApi.Test.Fixture;

namespace PrismaApi.Test.ControllerTests;

/// <summary>
/// The endpoint-count pass-through (eqx_prisma F019): it sends the restricted
/// influence diagram to the FastAPI route and returns its answer unchanged.
/// The FastAPI side is replaced by a recording fake.
/// </summary>
[Collection(nameof(PrismaCollection))]
public class StructureEndpointCountTests : PrismaApiControllerTestBase
{
    private readonly PrismaApiFixture _fixture;

    public StructureEndpointCountTests(PrismaApiFixture fixture) : base(fixture)
    {
        _fixture = fixture;
    }

    private HttpClient ClientWith(RecordingFastApiService fake) =>
        _fixture.ApiFactory.WithWebHostBuilder(builder => builder.ConfigureTestServices(services =>
        {
            services.RemoveAll<IFastApiService>();
            services.AddSingleton<IFastApiService>(fake);
        })).CreateClient();

    private static async Task<(HttpStatusCode Status, string Body)> GetAsync(HttpClient client, string uri)
    {
        var request = new HttpRequestMessage(HttpMethod.Get, uri);
        TestClientScope.AddHeaders(request);
        var response = await client.SendAsync(request);
        return (response.StatusCode, await response.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task GetEndpointCount_SendsTheRestrictedDiagramAndReturnsTheCount()
    {
        using var scope = _fixture.UserScope();
        var fake = new RecordingFastApiService(HttpStatusCode.OK, "{\"status\": \"ok\", \"ep\": \"5\"}");
        var projectId = _fixture.TestArgs.TestProjectId;

        var (status, body) = await GetAsync(ClientWith(fake), $"structure/{projectId}/endpoint_count");

        Assert.Equal(HttpStatusCode.OK, status);
        Assert.Contains("\"ep\": \"5\"", body);
        Assert.Equal($"/structure/{projectId}/endpoint_count", fake.LastEndpoint);
        Assert.True(fake.LastRestrict);
    }

    [Fact]
    public async Task GetEndpointCount_ForwardsDebugInput()
    {
        using var scope = _fixture.UserScope();
        var fake = new RecordingFastApiService(HttpStatusCode.OK, "{\"problem\": \"dtcount-problem 1\"}");
        var projectId = _fixture.TestArgs.TestProjectId;

        await GetAsync(ClientWith(fake), $"structure/{projectId}/endpoint_count?debug=input");

        Assert.Equal($"/structure/{projectId}/endpoint_count?debug=input", fake.LastEndpoint);
    }

    [Fact]
    public async Task GetEndpointCount_PassesTheCounterUnavailableStatusThrough()
    {
        using var scope = _fixture.UserScope();
        var fake = new RecordingFastApiService(HttpStatusCode.ServiceUnavailable, "{\"message\": \"endpoint counter unavailable\"}");
        var projectId = _fixture.TestArgs.TestProjectId;

        var (status, body) = await GetAsync(ClientWith(fake), $"structure/{projectId}/endpoint_count");

        Assert.Equal(HttpStatusCode.ServiceUnavailable, status);
        Assert.Contains("unavailable", body);
    }

    public sealed class RecordingFastApiService(HttpStatusCode status, string content) : IFastApiService
    {
        public string? LastEndpoint { get; private set; }
        public bool? LastRestrict { get; private set; }

        public Task<ApiResponseDto> SendInfluenceDiagramToFastApiAsync(Guid projectId, string endpoint, UserOutgoingDto user, bool restrictInfluenceDiagram, CancellationToken ct = default)
        {
            LastEndpoint = endpoint;
            LastRestrict = restrictInfluenceDiagram;
            return Task.FromResult(new ApiResponseDto { StatusCode = status, Content = content });
        }

        public Task<ApiResponseDto> CallDownstreamFastApiGetAsync(string endpoint, CancellationToken ct = default) => throw new NotImplementedException();
        public Task<ApiResponseDto> CallDownstreamFastApiPostAsync(string endpoint, StringContent content, CancellationToken ct = default) => throw new NotImplementedException();
        public Task<ApiResponseDto> SendPartialInfluenceDiagramToFastApiAsync(Guid projectId, string endpoint, List<List<Guid>> paths, UserOutgoingDto user, CancellationToken ct = default) => throw new NotImplementedException();
        public Task<ApiResponseDto> SendInfluenceDiagramWithEvidenceToFastApiAsync(Guid projectId, string endpoint, List<EvidenceRequestDto> data, UserOutgoingDto user, CancellationToken ct = default) => throw new NotImplementedException();
        public Task<ApiResponseDto> SendInfluenceDiagramPolicyTableToFastApiAsync(Guid projectId, string endpoint, EvidenceRequestDto? evidence, UserOutgoingDto user, CancellationToken ct = default) => throw new NotImplementedException();
        public List<PolicyTableOutgoingDto> ParsePolicyTableResponse(string? content) => [];
    }
}
