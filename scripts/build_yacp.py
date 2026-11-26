"""Increment the YACP test version, compile, and preserve a uniquely named artifact."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import shlex
import sys
import uuid
from verify_yacp_image import verify as verify_image
from check_yacp_source import assert_canonical, build_lock, source_manifest
from patch_yacp_sdk import apply_patches

ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENTS = ("tiny", "x4pro", "x4c")
SIMULATORS = ("simulator", "simulator_x3", "simulator_x4_pro", "simulator_x4_pro_uc8279", "simulator_x4_classic")
X3_WAKE_ONLY_FLAG = "-DYACP_X3_WAKE_ONLY_TEST=1"
X3_WAKE_FIRST_TURN_FLAG = "-DYACP_X3_WAKE_ONLY_TEST=2"


def build(environment, pio, wsl_mirror=None, *, wsl_distro="Ubuntu",
          wsl_pio="/home/christophe/.platformio/penv/bin/pio", x3_wake_only=0):
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    simulator = environment in SIMULATORS
    if not simulator and environment not in ENVIRONMENTS:
        raise ValueError(f"Unsupported YACP build environment: {environment}")
    if x3_wake_only and environment != "tiny":
        raise ValueError("The X3 wake experiment requires tiny")
    diagnostic_flag = {1: X3_WAKE_ONLY_FLAG, 2: X3_WAKE_FIRST_TURN_FLAG}.get(x3_wake_only)
    diagnostic = {1: "x3-wake-only", 2: "x3-wake-first-turn"}.get(x3_wake_only)
    if simulator != bool(wsl_mirror):
        raise ValueError("A simulator build requires --wsl-mirror; firmware builds must not use it")
    if simulator:
        mirror = PurePosixPath(wsl_mirror)
        if not mirror.is_relative_to("/home") or mirror == PurePosixPath("/home") or ".." in mirror.parts:
            raise ValueError("Expected an existing, explicit mirror below /home")
        if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_. -]*", wsl_distro) or wsl_distro.endswith((" ", ".")):
            raise ValueError("Expected a registered WSL distribution name")
        if not PurePosixPath(wsl_pio).is_absolute() or ".." in PurePosixPath(wsl_pio).parts:
            raise ValueError("Expected an absolute PlatformIO executable path in WSL")
    executable = shutil.which('wsl' if simulator else pio)
    if not executable:
        raise ValueError(f"PlatformIO executable not found: {pio}")
    policy, _ = assert_canonical(ROOT)
    apply_patches(ROOT)
    provenance = source_manifest(ROOT)
    config = ROOT / "platformio.ini"
    text = config.read_bytes().decode("utf-8")
    pattern = r"(?m)^(crossink_version\s*=\s*)([^\s]+-yacp\.)(\d+)(\s*)$"
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1:
        raise ValueError("Expected one explicitly numbered YACP test version")
    match = matches[0]
    version = match[2] + str(int(match[3]) + 1)
    artifacts = ROOT / "qa-artifacts"
    artifacts.mkdir(exist_ok=True)
    log_path = artifacts / f"build-{environment}-{version}.log"
    # A unique log is reserved before changing the version or starting compilation.
    with log_path.open("x", encoding="utf-8") as log:
        updated = text[:match.start()] + match[1] + version + match[4] + text[match.end():]
        config.write_bytes(updated.encode("utf-8"))
        env = dict(os.environ, PYTHONUTF8="1", PYTHONIOENCODING="utf-8")
        if diagnostic_flag:
            env["PLATFORMIO_BUILD_FLAGS"] = "\n".join(
                flag for flag in (env.get("PLATFORMIO_BUILD_FLAGS"), diagnostic_flag) if flag
            )
        manifest_path = artifacts / f"source-{environment}-{version}.json"
        manifest_path.write_text(json.dumps(provenance, indent=2), encoding="utf-8")
        ticket_path = artifacts / f"ticket-{environment}-{version}.json"
        ticket = {"product": policy["product"], "version": version, "environment": environment,
                  "source_root": str(ROOT), "execution_root": wsl_mirror,
                  "source_sha256": provenance["sha256"], "consumed": False,
                  "session_id": uuid.uuid4().hex, "lease_active": True,
                  "owner_pid": os.getpid(), "owner_os": os.name,
                  "wsl_distro": wsl_distro if simulator else None,
                  "mirror_src": {row["path"]: row["sha256"] for row in provenance["files"]
                                 if row["path"].startswith("src/")}}
        with ticket_path.open("x", encoding="utf-8") as file:
            json.dump(ticket, file, indent=2)
        env["YACP_BUILD_TICKET"] = str(ticket_path)
        env["YACP_BUILD_SESSION"] = ticket["session_id"]
        print(f"Building {environment}: {version}", flush=True)
        if simulator:
            windows_root = '/mnt/' + ROOT.drive[0].lower() + ROOT.as_posix()[2:]
            # These are copy operations only. The canonical source stays on Windows.
            commands = [['test', '-d', wsl_mirror], ['test', '-x', wsl_pio]]
            for directory in ('src', 'scripts', 'lib', 'freeink-sdk', 'include', 'assets'):
                if not (ROOT / directory).is_dir():
                    continue
                command = ['rsync', '-a', '--exclude=.git', '--exclude=__pycache__', '--exclude=*.pyc']
                commands.append(command + [windows_root + '/' + directory + '/', wsl_mirror + '/' + directory + '/'])
            for filename in ('platformio.ini', 'platformio.simulator.ini', '.yacp-source.json'):
                commands.append(['cp', windows_root + '/' + filename, wsl_mirror + '/' + filename])
            linux_ticket = windows_root + '/' + ticket_path.relative_to(ROOT).as_posix()
            commands += [['cd', wsl_mirror], ['env', 'YACP_BUILD_TICKET=' + linux_ticket,
                          'YACP_BUILD_SESSION=' + ticket['session_id'],
                          'PLATFORMIO_BUILD_DIR=.pio-simulator/build',
                          'PLATFORMIO_LIBDEPS_DIR=.pio-simulator/libdeps',
                          wsl_pio, 'run', '-e', environment]]
            command = [executable, '-d', wsl_distro, '--', 'bash', '-lc', ' && '.join(shlex.join(c) for c in commands)]
        else:
            command = [executable, "run", "-e", environment]
        try:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        finally:
            # ESP32's IDF-to-Arduino build legitimately re-enters SCons. The
            # same live session may enter again; no later invocation may replay it.
            finished = json.loads(ticket_path.read_text(encoding="utf-8"))
            finished["lease_active"] = False
            ticket_path.write_text(json.dumps(finished, indent=2), encoding="utf-8")
    print("\n".join(log_path.read_text(encoding="utf-8", errors="replace").splitlines()[-22:]))
    if result.returncode:
        return result.returncode
    if source_manifest(ROOT)["sha256"] != provenance["sha256"]:
        raise ValueError("Firmware inputs changed during compilation; artifact is not accepted")
    ticket = json.loads(ticket_path.read_text(encoding="utf-8"))
    if not ticket.get("consumed"):
        raise ValueError("Compilation did not execute the YACP source guard")
    diagnostic_flags = {X3_WAKE_ONLY_FLAG, X3_WAKE_FIRST_TURN_FLAG}.intersection(ticket.get("build_flags", []))
    if diagnostic_flags != ({diagnostic_flag} if diagnostic_flag else set()):
        raise ValueError("X3 wake experiment flag does not match the build request")
    if simulator:
        # PlatformIO may clean another environment on a config change. Preserve
        # a versioned host program before the next build, just as for firmware.
        binary = Path('//wsl.localhost/' + wsl_distro + wsl_mirror + '/.pio-simulator/build/' + environment + '/program')
        destination = artifacts / 'bin' / f'YACP-{version}-{environment}.elf'
        destination.parent.mkdir(exist_ok=True)
        with binary.open('rb') as source, destination.open('xb') as output:
            shutil.copyfileobj(source, output)
        report = {'version': version, 'environment': environment, 'build_succeeded': True,
                  'program': '/mnt/' + destination.drive[0].lower() + destination.as_posix()[2:],
                  'wsl_distro': wsl_distro,
                  'bytes': destination.stat().st_size, 'sha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
                  'hardware_validated': False, 'migration_validated': False,
                  'source': provenance, 'build_ticket': ticket}
        destination.with_suffix('.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps({key: value for key, value in report.items() if key not in ('source', 'build_ticket')}, indent=2))
        return 0
    binary = Path(env.get("PLATFORMIO_BUILD_DIR", ROOT / ".pio/build")) / environment / "firmware.bin"
    identity = verify_image(binary.read_bytes(), environment, version)
    binary_dir = artifacts / "bin"
    if diagnostic:
        binary_dir /= diagnostic
    destination = binary_dir / f"YACP-{version}-{environment}.bin"
    destination.parent.mkdir(exist_ok=True)
    with binary.open("rb") as source, destination.open("xb") as output:
        shutil.copyfileobj(source, output)
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    report = {"version": version, "environment": environment, "bytes": destination.stat().st_size,
              "sha256": digest, "build_succeeded": True, "hardware_validated": False,
              "migration_validated": False, "identity": identity,
              "source": provenance, "build_ticket": ticket}
    if diagnostic:
        report["diagnostic"] = diagnostic
    # Preserve symbols for size/crash analysis before PlatformIO clears a target.
    with (binary.parent / "firmware.elf").open("rb") as source, destination.with_suffix(".elf").open("xb") as output:
        shutil.copyfileobj(source, output)
    destination.with_suffix(".json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key not in ('source', 'build_ticket')}, indent=2))
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("environment", choices=ENVIRONMENTS + SIMULATORS + ("all-local",))
    parser.add_argument("--pio", default="pio")
    parser.add_argument("--wsl-mirror")
    parser.add_argument("--wsl-distro", default="Ubuntu", help="WSL distribution used for simulator builds")
    parser.add_argument("--wsl-pio", default="/home/christophe/.platformio/penv/bin/pio",
                        help="Absolute path to PlatformIO inside the selected WSL distribution")
    diagnostics = parser.add_mutually_exclusive_group()
    diagnostics.add_argument("--x3-wake-only", action="store_const", const=1, dest="x3_wake_only",
                             help="Build a diagnostic Tiny image only for X3")
    diagnostics.add_argument("--x3-wake-first-turn", action="store_const", const=2, dest="x3_wake_only",
                             help="Build the X3 wake experiment with a live reader after preview")
    args = parser.parse_args()
    if args.x3_wake_only and args.environment != "tiny":
        parser.error("X3 wake diagnostics require tiny")
    with build_lock(ROOT):
        if args.environment == "all-local":
            if args.wsl_mirror:
                parser.error("all-local builds firmware only")
            for environment in ENVIRONMENTS:
                status = build(environment, args.pio)
                if status:
                    raise SystemExit(status)
            raise SystemExit(0)
        raise SystemExit(build(args.environment, args.pio, args.wsl_mirror,
                               wsl_distro=args.wsl_distro, wsl_pio=args.wsl_pio, x3_wake_only=args.x3_wake_only))
