#!/usr/bin/env bash
# Fetches the pinned third-party dependencies next to CMakeLists.txt:
#   JUCE/                         JUCE framework (AGPLv3 or commercial licence)
#   deps/nam                      Neural Amp Modeler core (MIT)
#   deps/eigen                    Eigen (MPL2, used header-only with EIGEN_MPL2_ONLY)
#   deps/clap-juce-extensions     CLAP wrapper for JUCE (MIT), with the CLAP SDK (MIT)
# Existing folders are left alone, so local checkouts can be used instead.
set -euo pipefail
cd "$(dirname "$0")/.."

JUCE_TAG="${JUCE_TAG:-8.0.10}"
NAM_TAG="${NAM_TAG:-v0.6.0}"
EIGEN_TAG="${EIGEN_TAG:-3.4.0}"
CLAP_JUCE_REF="${CLAP_JUCE_REF:-7adee3a1bd4684d4caa5601100e364abccff4b4d}"

clone_tag() {   # folder url tag
    if [ -d "$1" ]; then echo "keeping $1"; return; fi
    git clone --depth 1 --branch "$3" "$2" "$1"
}

clone_tag JUCE        https://github.com/juce-framework/JUCE.git               "$JUCE_TAG"
clone_tag deps/nam    https://github.com/sdatkinson/NeuralAmpModelerCore.git  "$NAM_TAG"
clone_tag deps/eigen  https://github.com/eigen-mirror/eigen.git               "$EIGEN_TAG"

if [ -d deps/clap-juce-extensions ]; then
    echo "keeping deps/clap-juce-extensions"
else
    git clone https://github.com/free-audio/clap-juce-extensions.git deps/clap-juce-extensions
    git -C deps/clap-juce-extensions checkout --quiet "$CLAP_JUCE_REF"
    git -C deps/clap-juce-extensions submodule update --init --recursive
fi
echo "dependencies ready"
