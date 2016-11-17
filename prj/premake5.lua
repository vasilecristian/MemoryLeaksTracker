include "../../premake/common/"


solution "MemoryLeaksTracker"
    addCommonConfig()
	

    
project "MemoryLeaksTracker"
    files
    {
        "../source/**.h",
		"../source/**.c",
		"../source/**.cpp",
        "../include/**.h",
		"../include/**.c",
		"../include/**.cpp",
    }
    includedirs
    {
        "../include/",
		"../source/",
    }
    kind "StaticLib"
    targetdir("../lib/" .. GetPathFromPlatform())
    if (IsWin32()) then
        defines {"WIN32"}
    end
    
    if IsWin8StoreApp() then
        -- WinRT
        removeflags { 'StaticRuntime' }
        flags { 'DisableWinRT' }
    end

    if IsIos() then
		files
		{
			"../source/**.m",
			"../source/**.mm",
			"../include/**.m",
			"../include/**.mm",
		}
        defines {"OS_IPHONE"}
        buildoptions { "-std=c++11 -stdlib=libc++ -x objective-c++ -Wno-error" }
		-- kind "WindowedApp"
		-- files { GetPathFromPlatform() .. "/Info.plist" }
	else
		-- kind "ConsoleApp"
	end

	if IsXCode() then
        addCommonXcodeBuildSettings()	
        
        xcodebuildsettings 
        {
            ["ONLY_ACTIVE_ARCH"] = "NO"
        }
        
        xcodebuildresources
        {
            "../data/**",
        }

        files 
        {
            "../data/**",
        }
        defines {"OS_IPHONE"}
		buildoptions { "-std=c++11 -stdlib=libc++ -x objective-c++ -Wno-error" }
		
    end

	