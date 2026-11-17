# Manual SSH log refresh (first implementation)

Base: upstream commit `25c7de6d8f6da2ce6a00882e5af70b4f331af4c5`, packaged as 24.11.0.1685.

## Usage

1. Open a downloaded log (including a WinSCP temporary copy).
2. Select **Link remote log...** in the toolbar.
3. Enter `ssh://user@host:22/absolute/path/app.log`.
4. A separate temporary local copy opens in another tab, inheriting the search text and search options.
5. Click **Refresh from server** to download a new snapshot into that tab. The search is rerun after indexing.

The original WinSCP copy is never written. The server is read using `cat`; this feature does not write or clear remote files.

## Requirements

OpenSSH `ssh` must be available in PATH on the machine running klogg. Establish and verify the connection in a terminal first, including the server host key. Use a key, an SSH agent, or an existing OpenSSH configuration. `BatchMode=yes` and `StrictHostKeyChecking=yes` prevent hidden password/host-key prompts. SSH aliases defined in the user's OpenSSH config can be used as the host.

On Windows this is the Windows OpenSSH client, not the WinSCP embedded connection. WinSCP sessions and saved passwords are not imported. Password-only login and interactive MFA are not supported by this first implementation; no passwords are collected or stored by klogg.

## Behavior and limitations

- Download is asynchronous and cancellable; at most one per linked tab.
- A 15-second connection timeout and 120-second total timeout are used.
- Full snapshots are downloaded (not incremental tailing); an active log is not a transactional snapshot.
- Bytes are staged on disk and atomically committed with QSaveFile only after successful SSH exit. Cancellation, transport failure, and staging/write errors preserve the last committed copy.
- Remote paths are quoted for a POSIX shell. Only absolute paths and validated host/user/port values are accepted. The remote server must provide POSIX `cat --`.
- Search text, flags and the tab are retained during refresh. Exact scroll position and marks across file rotation are not guaranteed.
- Links are in-memory for the current application window; reconnect/relink after restart. Temporary copies are removed when klogg exits.
- F5 still reloads the local file. The new **Refresh from server** button explicitly performs a remote download.
- A closed linked tab cancels its pending transfer. Relinking an existing linked tab is not yet supported; create a new link instead.

## Validation

Standalone transport tests:

```sh
cmake -S tests/remote -B build-remote
cmake --build build-remote
ctest --test-dir build-remote --output-on-failure
```

The fixture SSH executable is POSIX-only; run these tests on Linux/macOS. Tests cover URL rejection, shell escaping, enforced host-key/batch settings, successful replacement, partial-download failure, cancellation, empty logs, process crash and missing OpenSSH. They do not contact a real SSH server.

Development validation: the transport and tests compiled with GCC/C++17 against Qt 5.15.15 and Qt 6.8.2 and passed locally on both. Full application compilation and Windows/Qt6 GUI testing remain pending. No GitHub Actions were triggered for this contribution.

Manual GUI checks still required before a release:

- Open a WinSCP copy, link it, verify inherited filters and distinct local path.
- Append lines remotely, refresh twice, verify search results and no duplicate tab.
- Rotate/truncate/empty the log; verify replacement and search updates.
- Cancel a large download; simulate failed auth, unreachable host, disk full and host-key mismatch.
- Refresh two tabs and close one during transfer; confirm no crash and correct destination.
- Exercise on Windows x64 Qt6 and Ubuntu with a real OpenSSH key/agent connection.

This branch is a development prototype, not a packaged release.
