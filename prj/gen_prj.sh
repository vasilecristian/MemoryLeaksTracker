#!/bin/sh
#
chmod +x ../../premake/release/premake5
../../premake/release/premake5 --arch=ios --to=xcode4/ios xcode4
../../premake/release/premake5 --arch=macosx --to=xcode4/macosx xcode4
