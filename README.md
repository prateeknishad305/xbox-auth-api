# xbox-live-api-cpp

A C++20 personal Xbox Live API that answers with the same response shapes as the official/paid
Xbox contracts, for the account the operator signed in.

- No device code login: OAuth 2.0 authorization code flow with PKCE (S256).
- No Azure app registration: a public first-party Microsoft client id is used by default and is
  configurable with `XBOX_CLIENT_ID`.
- Own implementation: C++ only, no calls into the Node project.
- Reads the signed-in account only: profile, game library, achievements, activity, presence and
  subscription details.

## Stack

C++20, CMake 3.20+, libcurl, nlohmann/json, OpenSSL, Boost.Beast (asynchronous HTTP server).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

## Run

```bash
cp .env.example .env
TOKEN_FILE=data/tokens.json PORT=3001 ./build/xbox-api-server
```

The server reads its configuration from the environment. `.env` is a convenience reference; export
the values with your shell or a process manager before starting the binary.

## Sign in

### PKCE (recommended)

```bash
curl -s -X POST http://127.0.0.1:3001/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"email":"you@example.com"}'
```

The optional `email` only pre-selects the account in the Microsoft page (`login_hint`), the service
never sees or needs a password.

Open the returned `authorizationUrl` in a browser, sign in with the Xbox account, and Microsoft
redirects back to `XBOX_REDIRECT_URI` with `code` and `state`. The server completes the exchange
and stores the session in `TOKEN_FILE` (mode 0600).

### Login with a code

If the redirect cannot reach the service (the browser is on another machine, or the page fails to
load), paste the authorization code back into the API:

```bash
curl -s -X POST http://127.0.0.1:3001/api/auth/code \
  -H 'Content-Type: application/json' \
  -d '{"code":"<code>","state":"<state>"}'
```

`state` may be omitted when only one login is pending; the newest pending login is used then.

The default redirect target is `http://localhost:<port>/`. The first-party client id used by
default only accepts `localhost` redirect targets, so `127.0.0.1` and callback paths are rejected
by Microsoft; the server therefore also reads `code` and `state` from `/api/auth/callback` and from
the root path, and the code can be posted back manually:

```bash
curl -s "http://127.0.0.1:3001/api/auth/callback?code=<code>&state=<state>"
```

### Token import

If you already hold a Microsoft refresh token:

```bash
curl -s -X POST http://127.0.0.1:3001/api/auth/import \
  -H 'Content-Type: application/json' \
  -d '{"refreshToken":"<microsoft-refresh-token>"}'
```

### Login with a browser cookie

If the account is already signed in to Microsoft in your own browser, the service can turn that
session into an Xbox session without any interactive sign-in. Open `https://login.live.com` in the
browser, sign in, then copy the value of the `__Host-MSAAUTHP` cookie (Chrome: `F12` -> Application
-> Storage -> Cookies -> `https://login.live.com`; the cookie is `HttpOnly`, so the console
`document.cookie` does not show it):

```bash
curl -s -X POST http://127.0.0.1:3001/api/auth/cookie \
  -H 'Content-Type: application/json' \
  -d '{"cookie":"__Host-MSAAUTHP=<value>"}'
```

A Netscape `cookies.txt` export works as well, the service picks the `__Host-MSAAUTHP` entry out of
it. The cookie is only used for the authorization request and is never stored; the resulting
session is written to `TOKEN_FILE` with mode 0600.

The cookie flow always runs against the legacy Microsoft Live endpoints with the Xbox client id
`00000000402b5328` and the `MBI_SSL` scope, independent of the PKCE profile, so it also works for
accounts that reject the `XboxLive.*` scopes. The service requests the authorization code without
following the redirect (the code is read from the `Location` header) and sends the `scope` parameter
with the code exchange, because Microsoft answers `server_error` when the scope is missing.

### Legacy Xbox Live SDK mode

Some Microsoft accounts refuse the `XboxLive.*` scopes for the web client id with
`invalid_request` ("the application is a first party application, the user does not have consent").
The Xbox Live SDK client id `00000000402b5328` with the `service::user.auth.xboxlive.com::MBI_SSL`
scope is pre-authorized by the Xbox Live resource server and works for those accounts:

```bash
XBOX_LEGACY_AUTHORIZE=true \
XBOX_CLIENT_ID=00000000402b5328 \
XBOX_SCOPE='service::user.auth.xboxlive.com::MBI_SSL' \
XBOX_REDIRECT_URI=https://login.live.com/oauth20_desktop.srf \
XBOX_AUTHORIZE_URL=https://login.live.com/oauth20_authorize.srf \
XBOX_TOKEN_URL=https://login.live.com/oauth20_token.srf \
XBOX_PKCE=false \
./build/xbox-api-server
```

The redirect then points at the Microsoft desktop page, so the authorization code is copied from the
browser address bar and posted to `/api/auth/code`. The refresh token that comes back is long lived
and can be imported later with `/api/auth/import`.

### Login with email and password

The service can sign in with the Microsoft account email and password directly, without a browser
and without a device code:

```bash
curl -s -X POST http://127.0.0.1:3001/api/auth/password \
  -H 'Content-Type: application/json' \
  -d '{"email":"you@example.com","password":"<password>"}'
```

On success the response carries `authenticated`, `xuid` and `gamertag`. The password is used once,
in the Microsoft sign-in form request, and is never stored; only the resulting tokens are written to
`TOKEN_FILE` with mode 0600.

Microsoft sometimes asks for an extra verification step ("We have detected something unusual about
this sign-in", Authenticator approval, a security code by mail). The service does not try to work
around that check. It answers `409` with the error code `verification_required` and the sign-in URL
to use:

```json
{
  "error": "verification_required",
  "verificationUrl": "https://login.live.com/oauth20_authorize.srf?...",
  "channels": ["Email"],
  "resumeUrl": "/api/auth/fragment"
}
```

Finish that step in your own browser (the URL already carries the right client and scope). The
browser ends on `https://login.live.com/oauth20_desktop.srf#access_token=...&refresh_token=...`.
Copy the part after `#` and hand it to the API:

```bash
curl -s -X POST http://127.0.0.1:3001/api/auth/fragment \
  -H 'Content-Type: application/json' \
  -d '{"fragment":"access_token=...&refresh_token=...&expires_in=..."}'
```

Other outcomes of the password route are `401 invalid_credentials` (wrong email or password) and
`429 login_throttled` (Microsoft rate limited that sign-in method, wait before retrying).

The password route uses its own client profile, so it also works while the rest of the service runs
in PKCE or legacy mode. It can be switched off with `XBOX_PASSWORD_LOGIN=false`.

### Status and logout

```bash
curl -s http://127.0.0.1:3001/api/auth/status
curl -s -X POST http://127.0.0.1:3001/api/auth/logout
```

## Routes

| Method | Path | Notes |
| --- | --- | --- |
| GET | `/health` | Liveness probe |
| GET | `/api` | Route index |
| POST | `/api/auth/login` | Starts the PKCE flow, returns `authorizationUrl` and `state` |
| POST | `/api/auth/code` | Login with a code: body `{"code":"...","state":"..."}` |
| GET | `/api/auth/callback` | OAuth redirect target (`code`, `state`) |
| POST | `/api/auth/import` | Body: `{"refreshToken":"..."}` |
| POST | `/api/auth/cookie` | Body: `{"cookie":"__Host-MSAAUTHP=..."}` (Microsoft account session) |
| POST | `/api/auth/password` | Body: `{"email":"...","password":"..."}` (Microsoft sign-in form) |
| POST | `/api/auth/fragment` | Body: `{"fragment":"access_token=...&refresh_token=..."}` |
| GET | `/api/auth/status` | Auth state and per-token validity |
| POST | `/api/auth/logout` | Clears the stored session |
| GET | `/api/profile` | Profile settings (profile contract c3) |
| GET | `/api/titles` | Game library with achievement progress (titlehub c2) |
| GET | `/api/achievements?maxTitles=20` | Achievements across recent titles (achievements c2) |
| GET | `/api/achievements/{titleId}` | Achievements for one title |
| GET | `/api/activity?maxItems=20` | Recently played titles |
| GET | `/api/activity/presence` | Presence and devices (userpresence c3) |
| GET | `/api/subscriptions` | Tier, Game Pass, friends/followers (peoplehub c7) |

Errors use `{"error":"<code>","message":"<text>"}` with meaningful status codes:
`400` for bad input, `401` when the session is missing or rejected, `403` for accounts without an
Xbox profile, `404` for missing resources, `429` when Xbox Live rate limits, `502` for upstream
failures.

## Configuration

| Variable | Default | Purpose |
| --- | --- | --- |
| `PORT` | `3001` | Listen port |
| `XBOX_HOST` | `0.0.0.0` | Listen address |
| `XBOX_THREADS` | `4` | Worker threads for the event loop and the blocking work pool |
| `XBOX_LOG_LEVEL` | `info` | `debug`, `info`, `warn`, `error` |
| `TOKEN_FILE` | `data/tokens.json` | Token store path, always written with mode 0600 |
| `XBOX_CLIENT_ID` | public first-party id | Microsoft OAuth client id |
| `XBOX_CLIENT_SECRET` | empty | Only needed for confidential clients |
| `XBOX_SCOPE` | `XboxLive.signin XboxLive.offline_access offline_access` | OAuth scopes |
| `XBOX_REDIRECT_URI` | `http://localhost:<port>/` | OAuth redirect target |
| `XBOX_AUTHORIZE_URL` | Microsoft v2 authorize endpoint | Authorization endpoint, overridable for the legacy SDK flow |
| `XBOX_TOKEN_URL` | Microsoft v2 token endpoint | Token endpoint, overridable for the legacy SDK flow |
| `XBOX_PKCE` | `true` (`false` in legacy mode) | Sends `code_challenge` and `code_verifier` |
| `XBOX_LEGACY_AUTHORIZE` | `false` | Legacy defaults: SDK client id, `MBI_SSL` scope, desktop redirect |
| `XBOX_PASSWORD_LOGIN` | `true` | Enables `POST /api/auth/password` |
| `XBOX_PASSWORD_CLIENT_ID` | `000000004C12AE6F` | Client id used by the password sign-in form |
| `XBOX_PASSWORD_SCOPE` | `service::user.auth.xboxlive.com::MBI_SSL` | Scope used by the password sign-in form |
| `XBOX_PASSWORD_AUTHORIZE_URL` | `https://login.live.com/oauth20_authorize.srf` | Authorize endpoint of the password flow |
| `XBOX_PASSWORD_TOKEN_URL` | `https://login.live.com/oauth20_token.srf` | Token endpoint used to refresh those tokens |
| `XBOX_PASSWORD_REDIRECT_URI` | `https://login.live.com/oauth20_desktop.srf` | Redirect that carries the token fragment |
| `XBOX_COOKIE_CLIENT_ID` | `00000000402b5328` | Client id used by the cookie sign-in flow |
| `XBOX_COOKIE_SCOPE` | `service::user.auth.xboxlive.com::MBI_SSL` | Scope used by the cookie sign-in flow |
| `XBOX_COOKIE_AUTHORIZE_URL` | `https://login.live.com/oauth20_authorize.srf` | Authorize endpoint of the cookie flow |
| `XBOX_COOKIE_TOKEN_URL` | `https://login.live.com/oauth20_token.srf` | Token endpoint of the cookie flow |
| `XBOX_COOKIE_REDIRECT_URI` | `https://login.live.com/oauth20_desktop.srf` | Redirect that carries the authorization code |
| `XBOX_HTTP_TIMEOUT_MS` | `15000` | Xbox Live request timeout |
| `XBOX_HTTP_CONNECT_TIMEOUT_MS` | `8000` | Connect timeout |
| `XBOX_AUTH_TIMEOUT_MS` | `12000` | Token endpoint timeout |

## Token handling

- Tokens are never logged. The log writer redacts token fields before printing.
- The token store is written atomically with mode 0600 and keeps secrets out of the repository
  (`data/tokens.json` is git ignored).
- Expiry is tracked for the Microsoft access token, the Xbox Live token and the Xbox security token,
  so a stale token is refreshed instead of being reused.

## Notes on the paid contracts

Title catalog, entitlements, statistics and the other partner datasets require a paid Microsoft
partner account and are not available to a personal account. This service mirrors the public
contract shapes (profile c3, titlehub c2, achievements c2, userpresence c3, peoplehub c7) for the
signed-in account.
