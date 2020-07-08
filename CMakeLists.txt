cmake_minimum_required(VERSION 3.12)
project(ya_uftp LANGUAGES CXX)

set(Boost_ADDITIONAL_VERSIONS "1.69" "1.69.0" "1.70" "1.70.0" "1.71" "1.71.0")
set(Boost_USE_STATIC_LIBS ON)

find_package(Boost COMPONENTS filesystem)

set(libuftp_sender_src 
	"detail/core.cpp"
	"detail/progress_notification.cpp"
	"detail/message.cpp"
	"api_binder.cpp"
	"detail/file_transfer_base.cpp"
	"sender/detail/adi.cpp"
	"sender/detail/server.cpp" 
	"sender/detail/worker.cpp" 
	"sender/detail/files_delivery_session.cpp"
	"sender/detail/file_send_task.cpp"
	"sender/detail/session_context.cpp"
	"utilities/detail/network_intf.cpp"
	"ya_uftp.cpp"
	)
	
set(libuftp_receiver_src
	"detail/core.cpp"
	"detail/progress_notification.cpp"
	"detail/message.cpp"
	"api_binder.cpp"
	"detail/file_transfer_base.cpp"
	"utilities/detail/network_intf.cpp"
	"receiver/detail/adi.cpp"
	"receiver/detail/session_context.cpp"
	"receiver/detail/worker.cpp"
	"receiver/detail/announcement_monitor.cpp"
	"receiver/detail/file_receive_task.cpp"
	"receiver/detail/files_accept_session.cpp"
	"receiver/detail/server.cpp"
	"ya_uftp.cpp"
	)
	
add_library(uftp_sender ${libuftp_sender_src})
add_library(uftp_receiver ${libuftp_receiver_src})

set_property(TARGET uftp_sender PROPERTY CXX_STANDARD 17)
set_property(TARGET uftp_receiver PROPERTY CXX_STANDARD 17)

add_executable(sender_demo "example/sender_demo.cpp" ${libuftp_sender_src})
add_executable(receiver_demo "example/receiver_demo.cpp" ${libuftp_receiver_src})

set_target_properties(sender_demo receiver_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/example)

set_property(TARGET sender_demo PROPERTY CXX_STANDARD 17)
set_property(TARGET receiver_demo PROPERTY CXX_STANDARD 17)

if(MSVC)
	set(CMAKE_CXX_FLAGS "/permissive- /EHsc /D_WIN32_WINNT=0x0600")
	set(DEBUG_BUILD_FLAGS "/JMC")
	set(DEBUG_LINKER_FLAGS "/Profile")
	set(RELEASE_BUILD_FLAGS "/O2")
endif(MSVC)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
	target_link_libraries(sender_demo rt pthread)
	target_link_libraries(receiver_demo rt pthread)
	target_link_libraries(uftp_sender rt pthread)
	target_link_libraries(uftp_receiver rt pthread)
endif(CMAKE_SYSTEM_NAME MATCHES "Linux")

target_link_libraries(uftp_sender ${Boost_LIBRARIES})
target_link_libraries(uftp_receiver ${Boost_LIBRARIES})
target_link_libraries(sender_demo ${Boost_LIBRARIES})
target_link_libraries(receiver_demo ${Boost_LIBRARIES})

target_compile_options(uftp_sender PUBLIC "$<$<CONFIG:DEBUG>:${DEBUG_BUILD_FLAGS}>")
target_compile_options(uftp_sender PUBLIC "$<$<CONFIG:RELEASE>:${RELEASE_BUILD_FLAGS}>")
target_compile_options(uftp_receiver PUBLIC "$<$<CONFIG:DEBUG>:${DEBUG_BUILD_FLAGS}>")
target_compile_options(uftp_receiver PUBLIC "$<$<CONFIG:RELEASE>:${RELEASE_BUILD_FLAGS}>")


target_compile_options(sender_demo PUBLIC "$<$<CONFIG:DEBUG>:${DEBUG_BUILD_FLAGS}>")
target_compile_options(sender_demo PUBLIC "$<$<CONFIG:RELEASE>:${RELEASE_BUILD_FLAGS}>")
target_compile_options(receiver_demo PUBLIC "$<$<CONFIG:DEBUG>:${DEBUG_BUILD_FLAGS}>")
target_compile_options(receiver_demo PUBLIC "$<$<CONFIG:RELEASE>:${RELEASE_BUILD_FLAGS}>")


set_target_properties(uftp_sender PROPERTIES LINK_FLAGS "${DEBUG_LINKER_FLAGS}")
set_target_properties(uftp_receiver PROPERTIES LINK_FLAGS "${DEBUG_LINKER_FLAGS}")
set_target_properties(sender_demo PROPERTIES LINK_FLAGS "${DEBUG_LINKER_FLAGS}")
set_target_properties(receiver_demo PROPERTIES LINK_FLAGS "${DEBUG_LINKER_FLAGS}")

target_compile_definitions(sender_demo PRIVATE "BOOST_ALL_NO_LIB")
target_include_directories(sender_demo PRIVATE "${Boost_INCLUDE_DIR}" 
	 ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/dependency/include)


target_compile_definitions(receiver_demo PRIVATE "BOOST_ALL_NO_LIB")
target_include_directories(receiver_demo PRIVATE "${Boost_INCLUDE_DIR}" 
	 ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/dependency/include)

target_compile_definitions(uftp_sender PRIVATE "BOOST_ALL_NO_LIB")
target_include_directories(uftp_sender PRIVATE "${Boost_INCLUDE_DIR}" 
	 ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/dependency/include)


target_compile_definitions(uftp_receiver PRIVATE "BOOST_ALL_NO_LIB")
target_include_directories(uftp_receiver PRIVATE "${Boost_INCLUDE_DIR}" 
	 ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/dependency/include)

install(FILES sender/server.hpp sender/adi.hpp
	 DESTINATION include/sender)
install(FILES receiver/server.hpp receiver/adi.hpp
	 DESTINATION include/receiver)
install(FILES ya_uftp.hpp api_binder.hpp DESTINATION include)

install(TARGETS uftp_sender uftp_receiver
	ARCHIVE DESTINATION lib
	RUNTIME DESTINATION lib)