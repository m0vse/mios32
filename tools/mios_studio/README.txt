Currently prepared for Juce 7.0.7 which can be downloaded from
https://github.com/WeAreROLI/JUCE/releases

MacOS users: unpack it and move it to ~/JUCE (your home directory)
Linux users: unpack it and move it to ~/JUCE (your home directory)
Windows users: unpack it and move it to C:\JUCE

Windows / Visual Studio 2022
----------------------------

Set JUCE_PATH to the JUCE source directory, then configure and build with:

  cmake --preset vs2022-x64
  cmake --build --preset vs2022-x64-release

The generated Visual Studio 2022 solution is placed in
Builds/VisualStudio2022. JUCE_PATH may also be passed as a CMake cache value.

Notes:
- MacOS: we build for 10.9 to ensure that users with older MacOS versions can use the tool.

Dependencies
------------

Here are (possibly incomplete) lists of required packages to build MIOSStudio:

Ubuntu 18.10 | libasound2-dev libcurl4-gnutls-dev libfreetype6-dev libwebkit2gtk-4.0-dev
