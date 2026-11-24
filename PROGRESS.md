# Progress

## Completed modules

| Module | Files | Status |
| --- | --- | --- |
| Base64 / base64url | `src/core/base64.{h,cpp}` | Done, padding-free base64url, RFC 4648 vectors covered |
| Crypto | `src/core/crypto.{h,cpp}` | Done, SHA-256, PKCE S256 challenge, CSPRNG, URL encode/decode, clock |
| Config | `src/core/config.{h,cpp}` | Done, environment driven with sane defaults |
| Logging | `src/core/log.{h,cpp}` | Done, levels plus secret redaction in the log writer |
| JSON helpers | `src/core/json.{h,cpp}` | Done, defensive accessors and safe parse |
| HTTP client | `src/core/http.{h,cpp}` | Done, libcurl, timeouts, redirects, header capture, form encoding, cookie jar support |
| Cookie jar | `src/core/cookies.{h,cpp}` | Done, merges `Set-Cookie` replies into a request cookie header |
| Token store | `src/core/store.{h,cpp}` | Done, atomic write, mode 0600, per-token expiry helpers |
| Executor | `src/core/executor.{h,cpp}` | Done, fixed size thread pool for blocking upstream calls |
| Auth | `src/xbox/auth.{h,cpp}` | Done, PKCE login, token import, refresh, cookie login, email+password login, XBL and XSTS exchange with expiry tracking |
| MSA sign-in | `src/xbox/msa.{h,cpp}` | Done, Microsoft sign-in form flow (PPFT + urlPost), implicit token fragment, step-up detection, proof channel listing |
| API client | `src/xbox/client.{h,cpp}` | Done, XBL3.0 authorization, 401 retry with full re-exchange, 429 mapping |
| Endpoints | `src/xbox/endpoints.{h,cpp}` | Done, profile, titles, achievements, activity, presence, subscriptions; 60s titles cache, parallel achievement fetch (pool of 6) |
| HTTP service | `src/http/server.{h,cpp}` | Done, asynchronous Boost.Beast server, blocking work dispatched to the pool |
| Entry point | `src/main.cpp` | Done |
| Tests | `tests/test_main.cpp` | Done, base64url, SHA-256, PKCE vector, JSON, store round trip, mappings, form encoding, cookie jar, token fragment, password and cookie profile configuration |

## Completed endpoints

| Method | Path | Upstream contract | Status |
| --- | --- | --- | --- |
| GET | `/health` | local | Done |
| GET | `/api` | local | Done |
| POST | `/api/auth/login` | Microsoft OAuth 2.0 authorize (PKCE S256), optional `email` hint | Done |
| POST | `/api/auth/code` | Microsoft OAuth 2.0 token, code pasted as JSON | Done |
| GET | `/api/auth/callback` | Microsoft OAuth 2.0 token | Done |
| POST | `/api/auth/import` | Microsoft OAuth 2.0 token (refresh grant) | Done |
| POST | `/api/auth/cookie` | Microsoft account session cookie to OAuth code | Done |
| POST | `/api/auth/password` | Microsoft sign-in form (PPFT + urlPost), implicit token fragment | Done |
| POST | `/api/auth/fragment` | Browser redirect fragment to Xbox session | Done |
| GET | `/api/auth/status` | local | Done |
| POST | `/api/auth/logout` | local | Done |
| GET | `/api/profile` | profile.xboxlive.com c3 | Done |
| GET | `/api/titles` | titlehub.xboxlive.com c2 | Done |
| GET | `/api/achievements` | achievements.xboxlive.com c2 | Done |
| GET | `/api/achievements/{titleId}` | achievements.xboxlive.com c2/c1 | Done |
| GET | `/api/activity` | titlehub.xboxlive.com c2 | Done |
| GET | `/api/activity/presence` | userpresence.xboxlive.com c3 | Done |
| GET | `/api/subscriptions` | peoplehub.xboxlive.com c7 | Done |

## Verification

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` succeeds.
- `cmake --build build -j2` succeeds with `-Wall -Wextra -Wpedantic` and no warnings.
- `ctest --test-dir build --output-on-failure` passes (1 test binary, all assertions green):
  base64url vectors, SHA-256 vector, RFC 7636 PKCE challenge vector, URL encode/decode, JSON
  accessors, token store round trip with a 0600 mode check, expiry helpers, title and achievement
  mapping, form encoding, log redaction of token fields, cookie jar merge, token fragment parsing
  and the password and cookie profile configuration.
- Live smoke check on port 3399 and the default port 3001: `/health`, `/api`, `/api/auth/status`,
  `/api/auth/login` (PKCE URL with `code_challenge_method=S256` and the Xbox scopes),
  `/api/auth/logout`, unknown route and `/api/profile` without a session all returned the expected
  payloads.
- Live check against the Microsoft token endpoint with a dummy refresh token returned
  `401 {"error":"reauth_required"}` as designed.
- The token file on disk was verified to be mode `0600` (`stat` reported `600`).
- Live Microsoft sign-in was exercised end to end with a real consumer account in a headless
  Chromium session. The `GetCredentialType` reply for that account reports
  `HasPassword: 1` but `PrefCredential: 7` (passkey), which sends the browser straight to the
  Windows Hello / passkey screen. Forcing `PrefCredential: 1` in that response makes the same
  Microsoft page render its normal password form, the password is accepted, and the account then
  asks for a one-time security code sent to the recovery address, so the sign-in needs the code
  from the account owner.
- The live token endpoint accepted our request shape (client id, PKCE verifier, redirect uri and
  scopes): a dummy authorization code failed with `AADSTS7000012` ("grant obtained for a different
  tenant") instead of a client/redirect/scope error.
- Redirect target probe: the first-party client id accepts `http://localhost`, `http://localhost:<port>`
  and `http://localhost/`, but rejects `http://127.0.0.1:<port>` and any path under it such as
  `/api/auth/callback`. The default `XBOX_REDIRECT_URI` is therefore `http://localhost:<port>/`, and
  the server reads the callback parameters from both `/` and `/api/auth/callback`.
- The service never needs the account password: `POST /api/auth/login` accepts an optional `email`
  that is only forwarded as `login_hint`, and the authorization code can be handed back with
  `POST /api/auth/code` as `{"code":"...","state":"..."}` (the `state` may be omitted, then the
  newest pending login is used). Error paths were checked live: a wrong state returns
  `400 {"error":"unknown_state"}` and a missing code returns `400 {"error":"missing_code"}`.
- Live Microsoft sign-in was completed successfully with a real consumer account (identifier ->
  credential picker -> "Send a code to <account email>" -> 6 digit code -> accepted), which proves
  the browser side of the flow. The sign-in then fails at the consent step of the authorize request
  with `invalid_request`: "The application is a first party application, the user does not have
  consent, and users are not permitted to consent to first party applications."
- Scope probe on the consumer v2 authorize endpoint with the first-party client id:
  `service::user.auth.xboxlive.com::MBI_SSL` is rejected with `invalid_scope` (that scope only
  exists on the legacy live endpoint), and `XboxLive.signin XboxLive.offline_access offline_access`
  is rejected with the first-party consent error above. The remaining combinations are
  `XboxLive.signin XboxLive.offline_access` and `XboxLive.signin` alone, which is the direction
  that still needs a live check.
- The consent error is not scope specific: a live sign-in with `XboxLive.signin` alone also ends in
  `invalid_request` (first party consent), so the web client id cannot obtain an Xbox token for an
  account that never gave consent to the Xbox Live resource server.
- Client inventory probe against the legacy live authorize endpoint with the `MBI_SSL` scope and the
  `oauth20_desktop.srf` redirect: `00000000402b5328` (Xbox Live / Minecraft SDK client) renders the
  sign-in page, `d3590ed6-52b3-4102-aeff-aad2292ab01c` and `1950a258-227b-4e31-a9cf-717495945fc2`
  answer `invalid_scope` for `MBI_SSL`, and `67e9d811-3f57-4f27-8d61-24f7f4f6f9f9`,
  `00000000402b5327`, `00000000402b5329`, `7d1f3ea5-8e6f-4a4b-9f2a-0b6a4f9e3c11` and
  `00000000441b0c6f-9e77-4b8c-a9e8-4a5b1c9e2d70` answer `unauthorized_client`.
- The legacy endpoints are alive: `login.live.com/oauth20_authorize.srf` renders the sign-in page
  and `login.live.com/oauth20_token.srf` accepts the `authorization_code` and `refresh_token`
  grants (a dummy code only fails with `invalid_grant` on the code value).
- Live sign-in with the SDK client id and the `MBI_SSL` scope was completed end to end (identifier ->
  credential picker -> "Send a code to <account email>" -> 6 digit code -> accepted), followed by the
  "Secure your account with two-step verification" interstitial which is skipped with "Not now".
  The sign-in method is rate limited per account and client ("You've reached your limit with this
  sign-in method"), so one attempt has to run the whole flow in a single browser session.
- The service now supports the legacy flow through configuration: `XBOX_LEGACY_AUTHORIZE=true`
  switches the defaults to client id `00000000402b5328`, the `MBI_SSL` scope, the legacy authorize
  and token endpoints, the desktop redirect and PKCE off. `XBOX_AUTHORIZE_URL`, `XBOX_TOKEN_URL`
  and `XBOX_PKCE` can be set individually as well, and the Xbox Live ticket is built without
  duplicating the `d=` prefix.
- Reference research on the cookie based sign in (the `MCC2T` project) confirmed the whole chain:
  a Microsoft account session cookie (`__Host-MSAAUTHP`) turns into an authorization code on the
  legacy authorize endpoint, that code into a Microsoft access token on `oauth20_token.srf`, and
  that token into an Xbox Live token plus an Xbox security token. No interactive sign-in is needed
  for this step, so the per account sign-in rate limit does not apply.
- Two details from that reference were applied to this service: the legacy token request omits the
  `scope` parameter, and the Xbox Live ticket is sent raw first with `d=` as fallback, because the
  legacy access token already carries the prefix.
- `POST /api/auth/cookie` was added for this flow. It accepts the cookie as a header string or as a
  Netscape `cookies.txt` export, picks `__Host-MSAAUTHP` out of it, uses it only for the authorize
  request and never stores it. Live check with a dummy cookie returns
  `401 {"error":"authorization_failed"}`, an empty body returns `400 {"error":"missing_cookie"}`.
- The HTTP client now reports the effective URL after redirects, which is where the legacy
  authorize endpoint puts the authorization code.
- Email and password sign-in was researched and implemented. The chain is the Microsoft sign-in form
  flow: `GET https://login.live.com/oauth20_authorize.srf` with the Xbox app client id
  `000000004C12AE6F`, the `service::user.auth.xboxlive.com::MBI_SSL` scope and
  `response_type=token` returns the sign-in page with the `PPFT` token and the `urlPost` form
  target, and `POST urlPost` with `login`, `loginfmt`, `passwd` and `PPFT` answers with a redirect
  that carries `access_token` and `refresh_token` in the URL fragment. No device code, no Azure app
  and no browser are involved. The reference implementation of that chain is the `xboxlive-auth`
  project, the live behavior was verified against Microsoft before it was ported to C++.
- New module `src/xbox/msa.{h,cpp}` implements that flow: it follows the authorize redirects with a
  cookie jar, parses `PPFT` and `urlPost`, posts the credentials, reads the token fragment and
  detects the two cases that stop the flow, a wrong password and a step-up verification.
- New endpoints `POST /api/auth/password` and `POST /api/auth/fragment` expose it. The password is
  used once in the form request and is never stored, the token profile of the password flow
  (client id, scope, token endpoint) is stored with the tokens so that the refresh grant uses the
  same client.
- Live checks of the new route: a correct password for an account that Microsoft wants to verify
  again answers `409 {"error":"verification_required"}` with the sign-in URL and the proof channels
  that Microsoft offers (only `Email` for that account), an account that does not exist answers
  `401 {"error":"invalid_credentials"}`, a request without a password answers
  `400 {"error":"missing_credentials"}` and a broken fragment answers `400 {"error":"missing_fragment"}`.
- When Microsoft asks for the extra verification, the operator finishes it in their own browser with
  the sign-in URL from the response. The browser ends on
  `https://login.live.com/oauth20_desktop.srf#access_token=...&refresh_token=...`, that fragment is
  sent to `POST /api/auth/fragment` and the service completes the Xbox Live and Xbox security token
  exchange from it.
- The password flow cannot replace the interactive step-up: the `identity/confirm` page offers the
  Authenticator approval, a security code by mail and "Use a different verification option", and the
  service reports that step instead of working around it.
- The credential picker route ("Send a code to <account>") stays browser only. A form post
  without a password is rejected by Microsoft with "Please enter the password for your Microsoft
  account.", also with `LoginOptions=3`, so a code request cannot be scripted from the form flow.
- The cookie flow was fixed and completed end to end. Three details were wrong before:
  the cookie route used the PKCE profile instead of a dedicated legacy profile, the authorization
  request followed the redirect (which lets Microsoft serve the desktop page and invalidate the
  code), and the code exchange omitted the `scope` parameter. Microsoft answers
  `500 {"error":"server_error","error_description":"The request could not be completed. Please try
  again later."}` for a code exchange without the scope, while the same request with
  `scope=service::user.auth.xboxlive.com::MBI_SSL` answers `200` with an access token and a refresh
  token. The reference project that omits the scope is outdated on that point.
- The cookie flow now has its own profile (`XBOX_COOKIE_CLIENT_ID`, `XBOX_COOKIE_SCOPE`,
  `XBOX_COOKIE_AUTHORIZE_URL`, `XBOX_COOKIE_TOKEN_URL`, `XBOX_COOKIE_REDIRECT_URI`) with the legacy
  live endpoints and the SDK client id, so it works regardless of the PKCE settings. The authorize
  request does not follow the redirect, the code is read from the `Location` header of the `302`, and
  the code exchange sends the scope. Every code exchange of the service sends the scope now, also
  the legacy SDK mode of `XBOX_LEGACY_AUTHORIZE=true`, because Microsoft answers `server_error`
  without it.
- The Xbox security token reply stores the user hash in `DisplayClaims.xui[0]`, an array. The parser
  now reads the first entry of that array, before that the session ended with
  `502 {"error":"xsts_invalid_response"}`.
- Live verification of the whole service with a real account (`CorkWheat9558`, XUID
  `2535455651307883`): `POST /api/auth/cookie` answered `200 {"authenticated":true,...}` and all
  data endpoints answered `200` with the contract shapes: `/api/profile` (12 fields),
  `/api/titles` (`{"count":0,"titles":[]}`), `/api/achievements`, `/api/activity`,
  `/api/activity/presence` and `/api/subscriptions` (16 fields including `badges`, `rep` and
  `presence`). The account has no played titles, and the empty list was confirmed against
  titlehub directly, which answers `{"xuid":"...","titles":[]}`.
- The Microsoft access token, the Xbox Live token and the Xbox security token were expired in the
  token store and `/api/profile` still answered `200`: the service refreshed the Microsoft token
  with the stored profile (client `00000000402b5328`, `MBI_SSL`, `login.live.com/oauth20_token.srf`)
  and rebuilt the Xbox Live and security token chain.
- The `titlehub` endpoint requires an `Accept-Language` header, otherwise it answers
  `400 ["Request does not contain the Accept-Language header, with valid locale value"]`. The
  client sends `en-US` on every request.
- A failed token import used to destroy the running session, because the imported refresh token
  replaced the stored one before Microsoft answered. The import now validates the candidate token
  first and only replaces the stored profile and tokens when the refresh grant succeeds. Live
  check: a rejected import answers `401 {"error":"reauth_required"}` and `/api/profile` keeps
  answering `200` with the running session.

## Known limitations

- The paid partner datasets (title catalog, catalogs, entitlements, statistics) need a paid Microsoft
  partner account and are out of reach for a personal account; only the public contract shapes are
  reproduced.
- The default client id is a public first-party Microsoft sample id. Point `XBOX_CLIENT_ID` at your
  own client if Microsoft restricts it for your tenant.
- A live sign-in stops at the Microsoft "Help us protect your account" step, which sends a one-time
  security code to the recovery address of the account; the code has to be entered by the account
  owner. Accounts whose preferred credential is a passkey additionally show the Windows Hello
  screen, the password form can still be reached because the account keeps a password.
- The Microsoft sign-in method of one account is rate limited per client id, so repeated logins with
  the same client id hit "You've reached your limit with this sign-in method" until the window
  resets. The other account is unusable because its mailbox is not reachable for the security code.
- The email and password route cannot finish a sign-in that Microsoft wants to verify a second time.
  That step needs the Authenticator of the account owner or a security code in the mailbox of the
  account, so the service returns the sign-in URL and waits for the operator to finish it in a
  browser and paste the token fragment back.
- The cookie route needs a signed-in Microsoft session in the browser of the operator. The cookie is
  read once, used for the authorization request only and never written to disk.
