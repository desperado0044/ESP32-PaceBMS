# Reads the current git version (last tag + commits since + short hash, e.g. "v1.3.1" on a
# clean tag or "v1.3.1-2-gabc1234-dirty" with uncommitted changes) and exposes it as the
# GIT_VERSION macro (see WebUiServer.cpp) - updates automatically on every build, so it can
# never lag behind the actually-installed firmware the way a manually maintained version
# string would.
Import("env")
import subprocess


def get_git_version():
    try:
        return (
            subprocess.check_output(
                ["git", "describe", "--tags", "--always", "--dirty"],
                stderr=subprocess.DEVNULL,
                cwd=env["PROJECT_DIR"],
            )
            .strip()
            .decode("utf-8")
        )
    except Exception:
        return "unknown"


env.Append(CPPDEFINES=[("GIT_VERSION", '\\"%s\\"' % get_git_version())])
