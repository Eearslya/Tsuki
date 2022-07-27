include(FetchContent)

FetchContent_Declare(Luna
	GIT_REPOSITORY https://github.com/Eearslya/Luna.git
	GIT_TAG develop)
FetchContent_Declare(tinygltf
	GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
	GIT_TAG master)

set(LUNA_BUILD_EDITOR Off CACHE BOOL "" FORCE)
set(LUNA_DEBUG_VULKAN Off CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE Off CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Luna tinygltf)

add_executable(Tsuki)
target_link_libraries(Tsuki
	PRIVATE Luna tinygltf)

target_sources(Tsuki PRIVATE
	GltfLoader.cpp
	Primitives.cpp
	SceneHierarchyPanel.cpp
	SceneRenderer.cpp
	Tsuki.cpp
	UI.cpp)

add_custom_target(Run
	COMMAND Tsuki
	DEPENDS Tsuki
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
