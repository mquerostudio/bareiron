#!/usr/bin/env bash
# Builds and runs the C unit tests. Each tests/unit/test_*.c is its own
# program, linked against every server source except main.c.
# Usage: tests/unit/run.sh [test_name ...]
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/tests
names=("$@")
if [ ${#names[@]} -eq 0 ]; then
  for f in tests/unit/test_*.c; do names+=("$(basename "$f" .c)"); done
fi
srcs=$(ls src/*.c | grep -v '^src/main.c$')
extra=""
[ -f third_party/miniz/miniz.c ] && extra="third_party/miniz/miniz.c -Ithird_party/miniz -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES"
failed=0
for name in "${names[@]}"; do
  cc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wno-unused-function -Iinclude -Itests/unit $extra $srcs "tests/unit/$name.c" -lpthread -o "build/tests/$name" 2> "build/tests/$name.cc.log" \
    || { echo "BUILD FAIL $name"; cat "build/tests/$name.cc.log" | grep -E "error" | head -20; failed=1; continue; }
  if "build/tests/$name" > "build/tests/$name.log" 2>&1; then echo "PASS $name"
  else echo "FAIL $name"; tail -20 "build/tests/$name.log"; failed=1; fi
done
exit $failed
