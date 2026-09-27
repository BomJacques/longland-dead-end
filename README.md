# LONGLAND DEAD END

JUCE 9 VST3 audio effect: a dreamy rolling-memory processor that keeps the present readable while related musical moments return as softened, incomplete afterimages.

## Build on Windows

1. Open `LonglandDeadEnd.jucer` with the JUCE 9 Projucer, or use the bundled Projucer command line to resave it.
2. Open `Builds/VisualStudio2022/LonglandDeadEnd.sln` in Visual Studio 2022/2026.
3. Build the `Release` configuration. The VST3 is written to the generated build folder.

The project uses only JUCE modules and keeps all rolling memory and event storage preallocated after `prepareToPlay`.
