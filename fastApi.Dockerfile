# syntax=docker/dockerfile:1

ARG PYTHON_VERSION=3.11
FROM python:${PYTHON_VERSION}-slim as base

# Prevents Python from writing pyc files.
ENV PYTHONDONTWRITEBYTECODE=1

# Keeps Python from buffering stdout and stderr to avoid situations where
# the application crashes without emitting any logs due to buffering.
ENV PYTHONUNBUFFERED=1
ENV PYTHONPATH=/code

WORKDIR /code

RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends build-essential \
    curl \
    apt-utils \
    gnupg2 && \
    pip install --upgrade pip && \
    pip install poetry && \
    poetry config virtualenvs.create false && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*


# Copy necessary files for poetry install
COPY /PrismaFastApi/pyproject.toml /PrismaFastApi/poetry.lock* /code/

# Install project dependencies
RUN poetry install --no-root --only main

# Copy the rest of the application code
COPY /PrismaFastApi/ /code/

WORKDIR /code/src

# Endpoint counter (eqx_prisma F019): the vendored, standalone dtcount
# procedure, built with the compiler the image already has.
RUN g++ -std=gnu++17 -O2 -I vendor/dtcount/cpp/include vendor/dtcount/cpp/src/*.cpp \
        -o /usr/local/bin/dtcount && \
    /usr/local/bin/dtcount --version
ENV DTCOUNT_BIN=/usr/local/bin/dtcount

# Add a new group "non-root-group" with group id 1001 and user "non-root-user" with the same id
RUN groupadd --gid 1001 non-root-group && \
    useradd --uid 1001 --gid 1001 --create-home non-root-user

USER 1001

# Expose the port that the application listens on.
EXPOSE 8000

# Run the application.
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]
