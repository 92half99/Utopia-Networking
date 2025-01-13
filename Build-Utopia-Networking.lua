project "Utopia-Networking"
   kind "StaticLib"
   language "C++"
   cppdialect "C++20"
   targetdir "bin/%{cfg.buildcfg}"
   staticruntime "off"

   files { "Source/**.h", "Source/**.hpp", "Source/**.cpp" }

   includedirs
   {
      "Source",

      "vendor/GameNetworkingSockets/include",

      --------------------------------------------------------
      -- Utopia includes
      -- Assumes we are in Utopia-Modules/Utopia-Networking
      "../../Utopia/Source",

      "../../vendor/imgui",
      "../../vendor/glfw/include",
      "../../vendor/glm",
      "../../vendor/spdlog/include",
      --------------------------------------------------------
   }

   targetdir ("../../bin/" .. outputdir .. "/%{prj.name}")
   objdir ("../../bin-int/" .. outputdir .. "/%{prj.name}")

   filter "system:windows"
      systemversion "latest"
      defines { "UT_PLATFORM_WINDOWS" }
      files { "Platform/Windows/**.h", "Platform/Linux/**.hpp", "Platform/Windows/**.cpp" }
      includedirs { "Platform/Windows" }
      links { "Ws2_32.lib" }

      -- Add UTF-8 compiler flag for Windows
      buildoptions { "/utf-8" }

   filter "system:linux"
      defines { "UT_PLATFORM_LINUX" }
      files { "Platform/Linux/**.h", "Platform/Linux/**.hpp", "Platform/Linux/**.cpp" }
      includedirs { "Platform/Linux" }

   filter { "system:windows", "configurations:Debug" }    
      links
      {
          "vendor/GameNetworkingSockets/bin/Windows/Debug/GameNetworkingSockets.lib"
      }

   filter { "system:windows", "configurations:Release or configurations:Dist" }    
      links
      {
          "vendor/GameNetworkingSockets/bin/Windows/Release/GameNetworkingSockets.lib"
      }

   filter "configurations:Debug"
      defines { "UT_DEBUG" }
      runtime "Debug"
      symbols "On"

   filter "configurations:Release"
      defines { "UT_RELEASE" }
      runtime "Release"
      optimize "On"
      symbols "On"

   filter "configurations:Dist"
      defines { "UT_DIST" }
      runtime "Release"
      optimize "On"
      symbols "Off"
