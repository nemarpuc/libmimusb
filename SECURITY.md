# Security Policy

## Supported Versions

Only the [latest release](https://github.com/nemarpuc/libmimusb/releases/latest)
receives security fixes. Before 1.0.0 a fix may come with a change of the
minor number. The board must run the firmware of the same minor version as the
host library.

## Reporting Vulnerabilities

Report security vulnerabilities privately through the repository's
[GitHub Security Advisories form](https://github.com/nemarpuc/libmimusb/security/advisories/new).
Do not include serial numbers, hardware MAC addresses, or raw USB packets containing
real user input in public issues.

## Sensitive Input Protection

Input reports can contain sensitive information, including keystrokes, credentials,
touch coordinates, and accessibility actions. `libmimusb` does not print input
report payloads to its standard log outputs. Applications and tests using `libmimusb`
should never write raw report buffers into persistent application logs, crash reports,
or public bug trackers; reproduce bugs with synthetic, non-sensitive input.

## System Device Permissions

Serial device nodes (`/dev/ttyUSB*`, `/dev/ttyACM*` on Linux, `/dev/cu.*` on macOS) and COM ports
on Windows are privileged system resources. Any process that can open the port can inject keystrokes
and touch input into the target, so restrict access to trusted users. On Linux, the rules provided in `udev/51-mimusb.rules` configure
desktop seat permissions (`TAG+="uaccess"`) and group access (mode `0660`) for specific USB-UART bridge IDs only. Audit and review
these rules against your target environment's access control policies before deployment.
