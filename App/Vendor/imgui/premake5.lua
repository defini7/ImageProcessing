---@diagnostic disable: undefined-global

include "../../../Engine/Vendor/glfw"

project "ImGui"
    kind "StaticLib"
    language "C++"
	cppdialect "C++20"
    staticruntime "On"
    warnings "off"

    targetdir ("%{wks.location}/Build/Target/" .. OUTPUT_DIR .. "/%{prj.name}")
    objdir ("%{wks.location}/Build/Obj/" .. OUTPUT_DIR .. "/%{prj.name}")

    includedirs { "../../../Engine/Vendor/glfw/include" }
    files { "src/*" }

    systemversion "latest"
    defines { "_GLFW_WIN32" }

    filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "speed"