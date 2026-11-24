#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export PATH="${JAVA_HOME:+$JAVA_HOME/bin:}$PATH"
classes=$(mktemp -d)
trap 'rm -rf "$classes"' EXIT
javac --release 8 -Xlint:-options -encoding UTF-8 -d "$classes" \
    src/local/plan/tasks/Task.java src/local/plan/tasks/DavClient.java tests/DavClientTest.java
java -cp "$classes" local.plan.tasks.DavClientTest
