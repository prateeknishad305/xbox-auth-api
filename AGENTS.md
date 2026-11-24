# Project Goal

Build a **C++ Xbox Live personal API** that answers with the **same response shapes as the official/paid Xbox API contracts**, with:

- **No device code login.** The account is authenticated with the OAuth 2.0 authorization code flow (PKCE) against the Microsoft consumer endpoint, or with a pasted refresh token (token import).
- **No Azure app registration of the operator.** The client id is a public first-party Microsoft client id that is configurable at runtime; no app has to be registered in the portal for the service to run.
- **Its own implementation.** No call to the Node project `xbox-live-api`, no wrapper around it: the HTTP client, the OAuth flow, the XBL/XSTS token chain and the parsers are written in C++.
- **The data of the signed in account only:** profile, game library, achievements, activity, presence and the subscription details (tier, Game Pass, friends/followers).

# Tech Stack

- Runtime: C++20, CMake 3.20+, no runtime dependency on Node.
- HTTP client: libcurl (multi handle, non-blocking I/O, timeouts, retry with backoff).
- JSON: nlohmann/json.
- Crypto: OpenSSL (PKCE S256, base64url, random state).
- Service: Boost.Beast (asynchronous, header only).

# Strict Rules

1. Execution speed, non-blocking I/O and clean error handling come first.
2. Modular code: one job per module, do not rewrite working modules.
3. Tokens are secrets: never log them, write them with mode 0600, keep them out of the repository.
4. No authentication bypass: the service only ever uses tokens of the account that the operator signed in, through the documented Microsoft OAuth flows.
5. Before finishing a task: build, run the tests, and update `PROGRESS.md` with the completed modules and endpoints.
