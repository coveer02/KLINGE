# Third-party software in KLINGE

KLINGE contains or links the following software. The full licence texts are in the
respective projects (fetched by `tools/fetch-deps.sh`) and, for the fonts, in `Resources/`.

| Component | Version | Licence | Copyright |
|---|---|---|---|
| JUCE | 8.0.10 | AGPLv3 or commercial JUCE licence | Raw Material Software Limited |
| Neural Amp Modeler Core | 0.6.0 | MIT | 2023 Steven Atkinson |
| nlohmann/json (bundled with NAM Core) | 3.12.0 | MIT | 2013-2025 Niels Lohmann |
| Eigen (header-only, built with `EIGEN_MPL2_ONLY`) | 3.4.0 | MPL 2.0 | Eigen contributors |
| clap-juce-extensions | 7adee3a | MIT | 2019-2020 Paul Walker |
| CLAP SDK and clap-helpers | 1.2.7 | MIT | 2021 Alexandre Bique |
| Space Grotesk (font) | | SIL Open Font License 1.1 | The Space Grotesk Project Authors |
| JetBrains Mono (font) | | SIL Open Font License 1.1 | The JetBrains Mono Project Authors |

## About JUCE's licence

JUCE 8 is available under the AGPLv3 or under a commercial licence from Raw Material Software.
Builds of KLINGE for your own use are fine either way. If you distribute KLINGE (free or paid),
you either publish the complete source under the AGPLv3 or hold a JUCE licence that covers your use.

## Eigen and MPL 2.0

Eigen is used unmodified as header-only code. MPL 2.0 only requires that the Eigen source files
stay available under the MPL; that is the case at https://gitlab.com/libeigen/eigen.

## MIT licence (NAM Core, nlohmann/json, clap-juce-extensions, CLAP)

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Neural Amp Modeler profiles

KLINGE loads `.nam` files but ships none. Profiles you download keep the licence their authors gave them.
