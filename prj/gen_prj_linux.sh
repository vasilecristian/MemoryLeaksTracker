#!/bin/bash

chmod +x ../../premake/release/premake5_linux
../../premake/release/premake5_linux --arch=x32 --to=gmake gmake
