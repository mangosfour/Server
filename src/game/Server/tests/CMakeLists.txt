# SPDX-License-Identifier: GPL-3.0-or-later
#
# MaNGOS is a full featured server for World of Warcraft, supporting
# the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
#
# Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Eluna's lualib is only built when SCRIPT_LIB_ELUNA is on, so a test that names
# the target unconditionally makes CMake generation fail outright with Eluna off
# -- not just the test, the whole configure. Resolve it once, conditionally.
set(MOP_TEST_LUA_PATH "")
if(TARGET lualib)
    set(MOP_TEST_LUA_PATH ";PATH=path_list_prepend:$<TARGET_FILE_DIR:lualib>")
endif()

# MopWireCodec.h/MopHandshake.h/MopFrameReader.h relocated to src/proto/ (Stage 2 CP2,
# ACE removal) -- they were already game-free and are proto's framing/handshake layer
# now. Add proto's include dir; the bare quote-includes in mop_framing_test.cpp are
# unchanged, they just resolve one directory further afield.
add_executable(mop_framing_test mop_framing_test.cpp)
target_include_directories(mop_framing_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/proto)
set_target_properties(mop_framing_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME mop_framing COMMAND mop_framing_test)

add_executable(script_action_fault_key_test script_action_fault_key_test.cpp)
target_include_directories(script_action_fault_key_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers)
set_target_properties(script_action_fault_key_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME script_action_fault_key COMMAND script_action_fault_key_test)

# MopAuthSession.cpp/MopAuthProof.cpp/MopSocketDrain.h relocated to src/proto/ alongside
# MopWireCodec/MopFrameReader/MopHandshake (same move); MopAuthResponse.{h,cpp} stays
# here in Server/ because its .cpp includes Opcodes.h/SharedDefines.h. Compiling the two
# moved .cpp files directly (as before) rather than linking the proto library avoids a
# second definition of MopAuth::DecodeAuthSession/ComputeAuthProof turning up when this
# target also links 'game' (which links proto): the linker only pulls archive members to
# resolve symbols not yet defined, so once these two TUs define them directly nothing
# from libproto.a is extracted for them.
add_executable(mop_auth_test
    mop_auth_test.cpp
    ${CMAKE_SOURCE_DIR}/src/proto/MopAuthSession.cpp
    ${CMAKE_SOURCE_DIR}/src/proto/MopAuthProof.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MopAuthResponse.cpp)
target_include_directories(mop_auth_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/proto
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_auth_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# MopAuthResponse.cpp includes Opcodes.h, which (per its own comment, ":39") includes the FULL
# WorldSession.h so the opcode table's OpcodeHandler struct size matches across translation units.
# That chain pulls in Item.h/AuctionHouseMgr.h/LFGMgr.h/DBCStructure.h/Object.h/Eluna's LuaValue.h --
# i.e. most of the game module's own header surface. Adding those include dirs one at a time here
# (Object, then AuctionHouseBot, then vmap, then Eluna, ...) chases an unbounded list; linking the
# real 'game' target instead inherits its already-correct PUBLIC include set (and, when
# SCRIPT_LIB_ELUNA is on, Eluna's own include dirs) via CMake usage requirements, which is bounded
# and matches how Server/*.cpp already compiles inside the real game library. mop_auth_test itself
# calls nothing from 'game' -- only enum values and struct layouts are used -- so no game .obj
# beyond what the headers require gets pulled into the test binary.
target_link_libraries(mop_auth_test PRIVATE game)
add_test(NAME mop_auth COMMAND mop_auth_test)

add_executable(mop_charenum_test mop_charenum_test.cpp)
target_include_directories(mop_charenum_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_charenum_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# Same rationale as mop_auth_test: WorldPacket/Opcodes pull the game header surface; link 'game'.
target_link_libraries(mop_charenum_test PRIVATE game)
add_test(NAME mop_charenum COMMAND mop_charenum_test)

add_executable(mop_trainerbuyfailed_test mop_trainerbuyfailed_test.cpp)
target_include_directories(mop_trainerbuyfailed_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_trainerbuyfailed_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# Same rationale as mop_auth_test: WorldPacket/Opcodes pull the game header surface; link 'game'.
target_link_libraries(mop_trainerbuyfailed_test PRIVATE game)
add_test(NAME mop_trainerbuyfailed COMMAND mop_trainerbuyfailed_test)

add_executable(mop_creategating_test mop_creategating_test.cpp)
target_include_directories(mop_creategating_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_creategating_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# Same rationale as mop_auth_test: Common.h/SharedDefines.h pull the game header surface; link 'game'.
target_link_libraries(mop_creategating_test PRIVATE game)
add_test(NAME mop_creategating COMMAND mop_creategating_test)

add_executable(mop_updateobject_test
    mop_updateobject_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../MopUpdateObject.cpp)
target_include_directories(mop_updateobject_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_updateobject_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# Same rationale as mop_auth_test: WorldPacket/Opcodes pull the game header surface; link 'game'.
target_link_libraries(mop_updateobject_test PRIVATE game)
add_test(NAME mop_updateobject COMMAND mop_updateobject_test)

add_executable(mop_transport_updateformap_packet_test
    mop_transport_updateformap_packet_test.cpp)
target_include_directories(mop_transport_updateformap_packet_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_transport_updateformap_packet_test
    PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_transport_updateformap_packet_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_transport_updateformap_packet_test PRIVATE shared)
add_test(NAME mop_transport_updateformap_packet
    COMMAND mop_transport_updateformap_packet_test)

add_executable(mop_client_opcode_worklist_test mop_client_opcode_worklist_test.cpp)
target_include_directories(mop_client_opcode_worklist_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_client_opcode_worklist_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_client_opcode_worklist_test PRIVATE game)
add_test(NAME mop_client_opcode_worklist COMMAND mop_client_opcode_worklist_test)

add_executable(mop_logout_packets_test mop_logout_packets_test.cpp)
target_include_directories(mop_logout_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_logout_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_logout_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_logout_packets_test PRIVATE shared)
add_test(NAME mop_logout_packets COMMAND mop_logout_packets_test)

add_executable(mop_compact_packets_test mop_compact_packets_test.cpp)
target_include_directories(mop_compact_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_compact_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_compact_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_compact_packets_test PRIVATE shared)
add_test(NAME mop_compact_packets COMMAND mop_compact_packets_test)

add_executable(mop_instance_reset_dispatch_test
    mop_instance_reset_dispatch_test.cpp)
target_include_directories(mop_instance_reset_dispatch_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_instance_reset_dispatch_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_instance_reset_dispatch_test PRIVATE game)
add_test(NAME mop_instance_reset_dispatch
    COMMAND mop_instance_reset_dispatch_test)
if(WIN32)
    get_filename_component(MOP_INSTANCE_RESET_MYSQL_RUNTIME_DIR
        "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_instance_reset_dispatch PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_INSTANCE_RESET_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_pet_set_action_capability_test
    mop_pet_set_action_capability_test.cpp)
target_include_directories(mop_pet_set_action_capability_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_pet_set_action_capability_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_pet_set_action_capability_test PRIVATE game)
add_test(NAME mop_pet_set_action_capability
    COMMAND mop_pet_set_action_capability_test)
if(WIN32)
    get_filename_component(MOP_PET_SET_ACTION_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_pet_set_action_capability PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_PET_SET_ACTION_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_mail_take_item_security_test
    mop_mail_take_item_security_test.cpp)
target_include_directories(mop_mail_take_item_security_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_mail_take_item_security_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_mail_take_item_security_test PRIVATE shared)
add_test(NAME mop_mail_take_item_security
    COMMAND mop_mail_take_item_security_test)

add_executable(mop_mail_packets_test mop_mail_packets_test.cpp)
target_include_directories(mop_mail_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_mail_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_mail_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_mail_packets_test PRIVATE shared)
add_test(NAME mop_mail_packets COMMAND mop_mail_packets_test)

add_executable(mop_mail_money_policy_test mop_mail_money_policy_test.cpp)
target_include_directories(mop_mail_money_policy_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers
    ${CMAKE_SOURCE_DIR}/src/shared
    ${CMAKE_SOURCE_DIR}/src/shared/Common)
set_target_properties(mop_mail_money_policy_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_mail_money_policy_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
add_test(NAME mop_mail_money_policy COMMAND mop_mail_money_policy_test)

add_executable(mop_lfg_state_policy_test mop_lfg_state_policy_test.cpp)
target_include_directories(mop_lfg_state_policy_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game/WorldHandlers
    ${CMAKE_SOURCE_DIR}/src/shared
    ${CMAKE_SOURCE_DIR}/src/shared/Common)
set_target_properties(mop_lfg_state_policy_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME mop_lfg_state_policy COMMAND mop_lfg_state_policy_test)

add_executable(mop_combat_log_packets_test mop_combat_log_packets_test.cpp)
target_include_directories(mop_combat_log_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_combat_log_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_combat_log_packets_test PRIVATE game)
add_test(NAME mop_combat_log_packets COMMAND mop_combat_log_packets_test)

add_executable(mop_aura_packets_test mop_aura_packets_test.cpp)
target_include_directories(mop_aura_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_aura_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_aura_packets_test PRIVATE game)
add_test(NAME mop_aura_packets COMMAND mop_aura_packets_test)

add_executable(mop_respec_packets_test mop_respec_packets_test.cpp)
target_include_directories(mop_respec_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_respec_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_respec_packets_test PRIVATE game)
add_test(NAME mop_respec_packets COMMAND mop_respec_packets_test)

add_executable(mop_party_stats_packets_test mop_party_stats_packets_test.cpp)
target_include_directories(mop_party_stats_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_party_stats_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_party_stats_packets_test PRIVATE game)
add_test(NAME mop_party_stats_packets COMMAND mop_party_stats_packets_test)

add_executable(mop_stable_packets_test mop_stable_packets_test.cpp)
target_include_directories(mop_stable_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_stable_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_stable_packets_test PRIVATE game)
add_test(NAME mop_stable_packets COMMAND mop_stable_packets_test)

add_executable(mop_ready_check_packets_test mop_ready_check_packets_test.cpp)
target_include_directories(mop_ready_check_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_ready_check_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_ready_check_packets_test PRIVATE game)
add_test(NAME mop_ready_check_packets COMMAND mop_ready_check_packets_test)

add_executable(mop_group_marker_packets_test mop_group_marker_packets_test.cpp)
target_include_directories(mop_group_marker_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_group_marker_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_group_marker_packets_test PRIVATE game)
add_test(NAME mop_group_marker_packets COMMAND mop_group_marker_packets_test)

add_executable(mop_group_invite_response_packets_test
    mop_group_invite_response_packets_test.cpp)
target_include_directories(mop_group_invite_response_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_group_invite_response_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_group_invite_response_packets_test PRIVATE game)
add_test(NAME mop_group_invite_response_packets
    COMMAND mop_group_invite_response_packets_test)
if(WIN32)
    get_filename_component(MOP_GROUP_INVITE_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_group_invite_response_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_GROUP_INVITE_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_group_invite_popup_packets_test
    mop_group_invite_popup_packets_test.cpp)
target_include_directories(mop_group_invite_popup_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_group_invite_popup_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_group_invite_popup_packets_test PRIVATE game)
add_test(NAME mop_group_invite_popup_packets
    COMMAND mop_group_invite_popup_packets_test)
if(WIN32)
    get_filename_component(MOP_GROUP_POPUP_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_group_invite_popup_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_GROUP_POPUP_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_group_uninvite_packets_test
    mop_group_uninvite_packets_test.cpp)
target_include_directories(mop_group_uninvite_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_group_uninvite_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_group_uninvite_packets_test PRIVATE game)
add_test(NAME mop_group_uninvite_packets
    COMMAND mop_group_uninvite_packets_test)
if(WIN32)
    get_filename_component(MOP_GROUP_UNINVITE_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_group_uninvite_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_GROUP_UNINVITE_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_lfg_leave_packets_test
    mop_lfg_leave_packets_test.cpp)
target_include_directories(mop_lfg_leave_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_lfg_leave_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_lfg_leave_packets_test PRIVATE game)
add_test(NAME mop_lfg_leave_packets COMMAND mop_lfg_leave_packets_test)
if(WIN32)
    get_filename_component(MOP_LFG_LEAVE_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_lfg_leave_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_LFG_LEAVE_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_lfg_player_info_packets_test
    mop_lfg_player_info_packets_test.cpp)
target_include_directories(mop_lfg_player_info_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_lfg_player_info_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_lfg_player_info_packets_test PRIVATE game)
add_test(NAME mop_lfg_player_info_packets COMMAND mop_lfg_player_info_packets_test)
if(WIN32)
    get_filename_component(MOP_LFG_PI_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_lfg_player_info_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_LFG_PI_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_lfg_proposal_response_packets_test
    mop_lfg_proposal_response_packets_test.cpp)
target_include_directories(mop_lfg_proposal_response_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_lfg_proposal_response_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_lfg_proposal_response_packets_test PRIVATE game)
add_test(NAME mop_lfg_proposal_response_packets COMMAND mop_lfg_proposal_response_packets_test)
if(WIN32)
    get_filename_component(MOP_LFG_PR_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_lfg_proposal_response_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_LFG_PR_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_lfg_proposal_packets_test
    mop_lfg_proposal_packets_test.cpp)
target_include_directories(mop_lfg_proposal_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_lfg_proposal_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_lfg_proposal_packets_test PRIVATE game)
add_test(NAME mop_lfg_proposal_packets COMMAND mop_lfg_proposal_packets_test)
if(WIN32)
    get_filename_component(MOP_LFG_PROP_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_lfg_proposal_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_LFG_PROP_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_lfg_role_check_packets_test
    mop_lfg_role_check_packets_test.cpp)
target_include_directories(mop_lfg_role_check_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_lfg_role_check_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_lfg_role_check_packets_test PRIVATE game)
add_test(NAME mop_lfg_role_check_packets COMMAND mop_lfg_role_check_packets_test)
if(WIN32)
    get_filename_component(MOP_LFG_RC_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_lfg_role_check_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_LFG_RC_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_lfg_set_roles_packets_test
    mop_lfg_set_roles_packets_test.cpp)
target_include_directories(mop_lfg_set_roles_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_lfg_set_roles_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_lfg_set_roles_packets_test PRIVATE game)
add_test(NAME mop_lfg_set_roles_packets COMMAND mop_lfg_set_roles_packets_test)
if(WIN32)
    get_filename_component(MOP_LFG_SET_ROLES_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_lfg_set_roles_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_LFG_SET_ROLES_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_group_role_poll_packets_test
    mop_group_role_poll_packets_test.cpp)
target_include_directories(mop_group_role_poll_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_group_role_poll_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_group_role_poll_packets_test PRIVATE game)
add_test(NAME mop_group_role_poll_packets
    COMMAND mop_group_role_poll_packets_test)
if(WIN32)
    get_filename_component(MOP_GROUP_ROLE_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_group_role_poll_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_GROUP_ROLE_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_group_loot_method_packets_test
    mop_group_loot_method_packets_test.cpp)
target_include_directories(mop_group_loot_method_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_group_loot_method_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_group_loot_method_packets_test PRIVATE game)
add_test(NAME mop_group_loot_method_packets
    COMMAND mop_group_loot_method_packets_test)
if(WIN32)
    get_filename_component(MOP_GROUP_LOOT_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_group_loot_method_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_GROUP_LOOT_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_group_invite_request_packets_test
    mop_group_invite_request_packets_test.cpp)
target_include_directories(mop_group_invite_request_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_group_invite_request_packets_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_group_invite_request_packets_test PRIVATE game)
add_test(NAME mop_group_invite_request_packets
    COMMAND mop_group_invite_request_packets_test)
if(WIN32)
    get_filename_component(MOP_GROUP_INVITE_REQUEST_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_group_invite_request_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_GROUP_INVITE_REQUEST_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_auction_packets_test mop_auction_packets_test.cpp)
target_include_directories(mop_auction_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_auction_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_auction_packets_test PRIVATE game)
add_test(NAME mop_auction_packets COMMAND mop_auction_packets_test)

add_executable(mop_world_entry_packets_test mop_world_entry_packets_test.cpp)
target_include_directories(mop_world_entry_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_world_entry_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_world_entry_packets_test PRIVATE game)
add_test(NAME mop_world_entry_packets COMMAND mop_world_entry_packets_test)

# Two opcode symbols sharing one value in the same direction. DefC/DefS assert at
# runtime, but only for opcodes actually registered -- a collision between two
# unregistered names is invisible until someone registers the second, which is how
# MSG_MOVE_WORLDPORT_ACK on 0x00E0 (CMSG_CHAR_ENUM in 18414) hung every client.

add_executable(mop_addon_packets_test mop_addon_packets_test.cpp)
target_include_directories(mop_addon_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_addon_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_addon_packets_test PRIVATE game)
add_test(NAME mop_addon_packets COMMAND mop_addon_packets_test)

add_executable(mop_control_packets_test mop_control_packets_test.cpp)
target_include_directories(mop_control_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_control_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_control_packets_test PRIVATE game)
add_test(NAME mop_control_packets COMMAND mop_control_packets_test)

add_executable(mop_currency_packets_test mop_currency_packets_test.cpp)
target_include_directories(mop_currency_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_currency_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_currency_packets_test PRIVATE game)
add_test(NAME mop_currency_packets COMMAND mop_currency_packets_test)

add_executable(mop_initial_packets_test mop_initial_packets_test.cpp)
target_include_directories(mop_initial_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_initial_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_initial_packets_test PRIVATE game)
add_test(NAME mop_initial_packets COMMAND mop_initial_packets_test)

add_executable(mop_achievement_packets_test mop_achievement_packets_test.cpp)
target_include_directories(mop_achievement_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_achievement_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_achievement_packets_test PRIVATE game)
add_test(NAME mop_achievement_packets COMMAND mop_achievement_packets_test)

add_executable(mop_creature_query_test mop_creature_query_test.cpp)
target_include_directories(mop_creature_query_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_creature_query_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_creature_query_test PRIVATE game)
add_test(NAME mop_creature_query COMMAND mop_creature_query_test)

add_executable(mop_gameobject_query_test mop_gameobject_query_test.cpp)
target_include_directories(mop_gameobject_query_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_gameobject_query_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_gameobject_query_test PRIVATE game)
add_test(NAME mop_gameobject_query COMMAND mop_gameobject_query_test)

add_executable(mop_corpse_query_test mop_corpse_query_test.cpp)
target_include_directories(mop_corpse_query_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_corpse_query_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_corpse_query_test PRIVATE game)
add_test(NAME mop_corpse_query COMMAND mop_corpse_query_test)

add_executable(mop_quest_poi_query_test mop_quest_poi_query_test.cpp)
target_include_directories(mop_quest_poi_query_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game/Server
    ${CMAKE_SOURCE_DIR}/src/game)
set_target_properties(mop_quest_poi_query_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_quest_poi_query_test PRIVATE game)
add_test(NAME mop_quest_poi_query COMMAND mop_quest_poi_query_test)

add_executable(mop_quest_npc_query_test mop_quest_npc_query_test.cpp)
target_include_directories(mop_quest_npc_query_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game/Server
    ${CMAKE_SOURCE_DIR}/src/game)
set_target_properties(mop_quest_npc_query_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_quest_npc_query_test PRIVATE game)
add_test(NAME mop_quest_npc_query COMMAND mop_quest_npc_query_test)

add_executable(mop_name_query_test mop_name_query_test.cpp)
target_include_directories(mop_name_query_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_name_query_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_name_query_test PRIVATE game)
add_test(NAME mop_name_query COMMAND mop_name_query_test)

add_executable(mop_realmquery_test mop_realmquery_test.cpp)
target_include_directories(mop_realmquery_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_realmquery_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_realmquery_test PRIVATE game)
add_test(NAME mop_realmquery COMMAND mop_realmquery_test)

add_executable(mop_time_query_test mop_time_query_test.cpp)
target_include_directories(mop_time_query_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_time_query_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_time_query_test PRIVATE game)
add_test(NAME mop_time_query COMMAND mop_time_query_test)

add_executable(mop_mail_query_test mop_mail_query_test.cpp)
target_include_directories(mop_mail_query_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_mail_query_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_mail_query_test PRIVATE game)
add_test(NAME mop_mail_query COMMAND mop_mail_query_test)

add_executable(mop_rated_bg_packets_test mop_rated_bg_packets_test.cpp)
target_include_directories(mop_rated_bg_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_rated_bg_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_rated_bg_packets_test PRIVATE game)
add_test(NAME mop_rated_bg_packets COMMAND mop_rated_bg_packets_test)

add_executable(mop_calendar_packets_test mop_calendar_packets_test.cpp)
target_include_directories(mop_calendar_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_calendar_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_calendar_packets_test PRIVATE game)
add_test(NAME mop_calendar_packets COMMAND mop_calendar_packets_test)

add_executable(mop_party_update_packets_test mop_party_update_packets_test.cpp)
target_include_directories(mop_party_update_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_party_update_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_party_update_packets_test PRIVATE game)
add_test(NAME mop_party_update_packets COMMAND mop_party_update_packets_test)

add_executable(mop_guild_packets_test mop_guild_packets_test.cpp)
target_include_directories(mop_guild_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_guild_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_guild_packets_test PRIVATE game)
add_test(NAME mop_guild_packets COMMAND mop_guild_packets_test)

add_executable(mop_guild_bank_packets_test mop_guild_bank_packets_test.cpp)
target_include_directories(mop_guild_bank_packets_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/game/Object
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_guild_bank_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_guild_bank_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_guild_bank_packets_test PRIVATE shared)
add_test(NAME mop_guild_bank_packets COMMAND mop_guild_bank_packets_test)

add_executable(mop_chat_packets_test mop_chat_packets_test.cpp)
target_include_directories(mop_chat_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_chat_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_chat_packets_test PRIVATE game)
add_test(NAME mop_chat_packets COMMAND mop_chat_packets_test)

add_executable(mop_channel_packets_test mop_channel_packets_test.cpp)
target_include_directories(mop_channel_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_channel_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_channel_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_channel_packets_test PRIVATE shared)
add_test(NAME mop_channel_packets COMMAND mop_channel_packets_test)

add_executable(mop_trade_packets_test mop_trade_packets_test.cpp)
target_include_directories(mop_trade_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_trade_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_trade_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_trade_packets_test PRIVATE shared)
add_test(NAME mop_trade_packets COMMAND mop_trade_packets_test)

add_executable(mop_notification_packets_test mop_notification_packets_test.cpp)
target_include_directories(mop_notification_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_notification_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_notification_packets_test PRIVATE game)
add_test(NAME mop_notification_packets COMMAND mop_notification_packets_test)

add_executable(mop_lfg_packets_test mop_lfg_packets_test.cpp)
target_include_directories(mop_lfg_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_lfg_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_lfg_packets_test PRIVATE game)
add_test(NAME mop_lfg_packets COMMAND mop_lfg_packets_test)

add_executable(mop_transfer_abort_packets_test mop_transfer_abort_packets_test.cpp)
target_include_directories(mop_transfer_abort_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_transfer_abort_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_transfer_abort_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_transfer_abort_packets_test PRIVATE shared)
add_test(NAME mop_transfer_abort_packets COMMAND mop_transfer_abort_packets_test)

add_executable(mop_gm_ticket_packets_test mop_gm_ticket_packets_test.cpp)
target_include_directories(mop_gm_ticket_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_gm_ticket_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_gm_ticket_packets_test PRIVATE game)
add_test(NAME mop_gm_ticket_packets COMMAND mop_gm_ticket_packets_test)

add_executable(mop_gossip_packets_test mop_gossip_packets_test.cpp)
target_include_directories(mop_gossip_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_gossip_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_gossip_packets_test PRIVATE game)
add_test(NAME mop_gossip_packets COMMAND mop_gossip_packets_test)

add_executable(mop_vendor_packets_test mop_vendor_packets_test.cpp)
target_include_directories(mop_vendor_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_vendor_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_vendor_packets_test PRIVATE game)
add_test(NAME mop_vendor_packets COMMAND mop_vendor_packets_test)

add_executable(mop_inventory_packets_test mop_inventory_packets_test.cpp)
target_include_directories(mop_inventory_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_inventory_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_inventory_packets_test PRIVATE game)
add_test(NAME mop_inventory_packets COMMAND mop_inventory_packets_test)

add_executable(mop_loot_packets_test mop_loot_packets_test.cpp)
target_include_directories(mop_loot_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_loot_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_loot_packets_test PRIVATE game)
add_test(NAME mop_loot_packets COMMAND mop_loot_packets_test)

add_executable(mop_gameobject_interaction_packets_test
    mop_gameobject_interaction_packets_test.cpp)
target_include_directories(mop_gameobject_interaction_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_gameobject_interaction_packets_test
    PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_gameobject_interaction_packets_test PRIVATE game)
add_test(NAME mop_gameobject_interaction_packets
    COMMAND mop_gameobject_interaction_packets_test)

add_executable(mop_world_quest_interaction_packets_test
    mop_world_quest_interaction_packets_test.cpp)
target_include_directories(mop_world_quest_interaction_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_world_quest_interaction_packets_test
    PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_world_quest_interaction_packets_test PRIVATE game)
add_test(NAME mop_world_quest_interaction_packets
    COMMAND mop_world_quest_interaction_packets_test)

add_executable(mop_character_customize_packets_test mop_character_customize_packets_test.cpp)
target_include_directories(mop_character_customize_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_character_customize_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_character_customize_packets_test PRIVATE game)
add_test(NAME mop_character_customize_packets COMMAND mop_character_customize_packets_test)

add_executable(mop_battleground_timer_packets_test mop_battleground_timer_packets_test.cpp)
target_include_directories(mop_battleground_timer_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_battleground_timer_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_battleground_timer_packets_test PRIVATE game)
add_test(NAME mop_battleground_timer_packets COMMAND mop_battleground_timer_packets_test)

add_executable(mop_movement_packets_test
    mop_movement_packets_test.cpp
    ${CMAKE_SOURCE_DIR}/src/game/movement/MovementInfo.cpp)
target_include_directories(mop_movement_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_movement_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_movement_packets_test PRIVATE game)
add_test(NAME mop_movement_packets COMMAND mop_movement_packets_test)

add_executable(mop_taxi_status_packets_test mop_taxi_status_packets_test.cpp)
target_include_directories(mop_taxi_status_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_taxi_status_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_taxi_status_packets_test PRIVATE game)
add_test(NAME mop_taxi_status_packets COMMAND mop_taxi_status_packets_test)

add_executable(mop_taxi_mask_domain_test mop_taxi_mask_domain_test.cpp)
target_include_directories(mop_taxi_mask_domain_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_taxi_mask_domain_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_taxi_mask_domain_test PRIVATE game)
add_test(NAME mop_taxi_mask_domain_test COMMAND mop_taxi_mask_domain_test)
if(WIN32)
    get_filename_component(MOP_TAXI_MASK_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_taxi_mask_domain_test PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MOP_TAXI_MASK_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_taxi_persistence_test mop_taxi_persistence_test.cpp)
target_include_directories(mop_taxi_persistence_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_taxi_persistence_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_taxi_persistence_test PRIVATE game)
add_test(NAME mop_taxi_persistence_test COMMAND mop_taxi_persistence_test)
if(WIN32)
    get_filename_component(MOP_TAXI_PERSISTENCE_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_taxi_persistence_test PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MOP_TAXI_PERSISTENCE_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_taxi_menu_packets_test mop_taxi_menu_packets_test.cpp)
target_include_directories(mop_taxi_menu_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_taxi_menu_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_taxi_menu_packets_test PRIVATE game)
add_test(NAME mop_taxi_menu_packets COMMAND mop_taxi_menu_packets_test)
if(WIN32)
    get_filename_component(MOP_TAXI_MENU_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_taxi_menu_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MOP_TAXI_MENU_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_taxi_activation_packets_test mop_taxi_activation_packets_test.cpp)
target_include_directories(mop_taxi_activation_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_taxi_activation_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_taxi_activation_packets_test PRIVATE game)
add_test(NAME mop_taxi_activation_packets COMMAND mop_taxi_activation_packets_test)
if(WIN32)
    get_filename_component(MOP_TAXI_ACTIVATION_PACKETS_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_taxi_activation_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MOP_TAXI_ACTIVATION_PACKETS_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_taxi_activation_runtime_test mop_taxi_activation_runtime_test.cpp)
target_include_directories(mop_taxi_activation_runtime_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/proto
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_taxi_activation_runtime_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_taxi_activation_runtime_test PRIVATE game)
add_test(NAME mop_taxi_activation_runtime COMMAND mop_taxi_activation_runtime_test)
if(WIN32)
    get_filename_component(MOP_TAXI_ACTIVATION_MYSQL_RUNTIME_DIR
        "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_taxi_activation_runtime PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MOP_TAXI_ACTIVATION_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_taxi_completion_packets_test mop_taxi_completion_packets_test.cpp)
target_include_directories(mop_taxi_completion_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_taxi_completion_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_taxi_completion_packets_test PRIVATE game)
add_test(NAME mop_taxi_completion_packets COMMAND mop_taxi_completion_packets_test)
if(WIN32)
    get_filename_component(MOP_TAXI_COMPLETION_MYSQL_RUNTIME_DIR
        "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_taxi_completion_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MOP_TAXI_COMPLETION_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_taxi_express_packets_test mop_taxi_express_packets_test.cpp)
target_include_directories(mop_taxi_express_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_taxi_express_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_taxi_express_packets_test PRIVATE game)
add_test(NAME mop_taxi_express_packets COMMAND mop_taxi_express_packets_test)
if(WIN32)
    get_filename_component(MOP_TAXI_EXPRESS_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_taxi_express_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MOP_TAXI_EXPRESS_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_taxi_completion_lifecycle_test mop_taxi_completion_lifecycle_test.cpp)
target_include_directories(mop_taxi_completion_lifecycle_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/proto
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_taxi_completion_lifecycle_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_taxi_completion_lifecycle_test PRIVATE game)
add_test(NAME mop_taxi_completion_lifecycle COMMAND mop_taxi_completion_lifecycle_test)
if(WIN32)
    get_filename_component(MOP_TAXI_COMPLETION_LIFECYCLE_MYSQL_RUNTIME_DIR
        "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_taxi_completion_lifecycle PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MOP_TAXI_COMPLETION_LIFECYCLE_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_death_packets_test mop_death_packets_test.cpp)
target_include_directories(mop_death_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_death_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_death_packets_test PRIVATE game)
add_test(NAME mop_death_packets COMMAND mop_death_packets_test)

add_executable(mop_quest_share_packets_test mop_quest_share_packets_test.cpp)
target_include_directories(mop_quest_share_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_quest_share_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_quest_share_packets_test PRIVATE game)
add_test(NAME mop_quest_share_packets COMMAND mop_quest_share_packets_test)

add_executable(mop_quest_completed_packets_test mop_quest_completed_packets_test.cpp)
target_include_directories(mop_quest_completed_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_quest_completed_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_quest_completed_packets_test PRIVATE game)
add_test(NAME mop_quest_completed_packets COMMAND mop_quest_completed_packets_test)

add_executable(mop_quest_log_packets_test mop_quest_log_packets_test.cpp)
target_include_directories(mop_quest_log_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_quest_log_packets_test
    PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_quest_log_packets_test PRIVATE game)
add_test(NAME mop_quest_log_packets COMMAND mop_quest_log_packets_test)

add_executable(mop_questgiver_acquisition_packets_test
    mop_questgiver_acquisition_packets_test.cpp)
target_include_directories(mop_questgiver_acquisition_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_questgiver_acquisition_packets_test
    PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_questgiver_acquisition_packets_test PRIVATE game)
add_test(NAME mop_questgiver_acquisition_packets
    COMMAND mop_questgiver_acquisition_packets_test)

add_executable(mop_questgiver_turnin_packets_test
    mop_questgiver_turnin_packets_test.cpp)
target_include_directories(mop_questgiver_turnin_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_questgiver_turnin_packets_test
    PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_questgiver_turnin_packets_test PRIVATE game)
add_test(NAME mop_questgiver_turnin_packets
    COMMAND mop_questgiver_turnin_packets_test)

add_executable(mop_spell_cast_packets_test mop_spell_cast_packets_test.cpp)
target_include_directories(mop_spell_cast_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_spell_cast_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_spell_cast_packets_test PRIVATE game)
add_test(NAME mop_spell_cast_packets COMMAND mop_spell_cast_packets_test)

add_executable(mop_use_item_packets_test mop_use_item_packets_test.cpp)
target_include_directories(mop_use_item_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_use_item_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_use_item_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_use_item_packets_test PRIVATE game shared)
add_test(NAME mop_use_item_packets COMMAND mop_use_item_packets_test)
if(WIN32)
    get_filename_component(MOP_SPELL_CAST_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_spell_cast_packets mop_use_item_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_SPELL_CAST_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_battlefield_status_packets_test mop_battlefield_status_packets_test.cpp)
target_include_directories(mop_battlefield_status_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_battlefield_status_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_battlefield_status_packets_test PRIVATE game)
add_test(NAME mop_battlefield_status_packets COMMAND mop_battlefield_status_packets_test)
if(WIN32)
    get_filename_component(MOP_BATTLEFIELD_STATUS_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_battlefield_status_packets PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MOP_BATTLEFIELD_STATUS_MYSQL_RUNTIME_DIR}${MOP_TEST_LUA_PATH}")
endif()

add_executable(mop_battle_pet_packets_test mop_battle_pet_packets_test.cpp)
target_include_directories(mop_battle_pet_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_battle_pet_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_battle_pet_packets_test PRIVATE game)
add_test(NAME mop_battle_pet_packets COMMAND mop_battle_pet_packets_test)

add_executable(mop_inspect_packets_test mop_inspect_packets_test.cpp)
target_include_directories(mop_inspect_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_inspect_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_inspect_packets_test PRIVATE game)
add_test(NAME mop_inspect_packets COMMAND mop_inspect_packets_test)
if(WIN32)
    get_filename_component(MOP_INSPECT_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_inspect_packets PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${MOP_INSPECT_MYSQL_RUNTIME_DIR}")
endif()

add_executable(mop_fishing_packets_test mop_fishing_packets_test.cpp)
target_include_directories(mop_fishing_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_fishing_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_fishing_packets_test PRIVATE shared)
add_test(NAME mop_fishing_packets COMMAND mop_fishing_packets_test)

add_executable(mop_barber_shop_packets_test mop_barber_shop_packets_test.cpp)
target_include_directories(mop_barber_shop_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_barber_shop_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_barber_shop_packets_test PRIVATE shared)
add_test(NAME mop_barber_shop_packets COMMAND mop_barber_shop_packets_test)

add_executable(mop_quest_failure_packets_test mop_quest_failure_packets_test.cpp)
target_include_directories(mop_quest_failure_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_quest_failure_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_quest_failure_packets_test PRIVATE shared)
add_test(NAME mop_quest_failure_packets COMMAND mop_quest_failure_packets_test)

add_executable(mop_quest_progress_packets_test mop_quest_progress_packets_test.cpp)
target_include_directories(mop_quest_progress_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_quest_progress_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_quest_progress_packets_test PRIVATE game)
add_test(NAME mop_quest_progress_packets COMMAND mop_quest_progress_packets_test)

add_executable(mop_player_progression_packets_test mop_player_progression_packets_test.cpp)
target_include_directories(mop_player_progression_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_player_progression_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_player_progression_packets_test PRIVATE game)
add_test(NAME mop_player_progression_packets COMMAND mop_player_progression_packets_test)

add_executable(mop_player_phase_test mop_player_phase_test.cpp)
target_include_directories(mop_player_phase_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_player_phase_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_player_phase_test PRIVATE game)
add_test(NAME mop_player_phase COMMAND mop_player_phase_test)

add_executable(mop_character_pane_packets_test mop_character_pane_packets_test.cpp)
target_include_directories(mop_character_pane_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_character_pane_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_character_pane_packets_test PRIVATE game)
add_test(NAME mop_character_pane_packets COMMAND mop_character_pane_packets_test)

add_executable(mop_world_state_packets_test mop_world_state_packets_test.cpp)
target_include_directories(mop_world_state_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_world_state_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_world_state_packets_test PRIVATE game)
add_test(NAME mop_world_state_packets COMMAND mop_world_state_packets_test)
if(WIN32)
    get_filename_component(MOP_WORLD_STATE_MYSQL_RUNTIME_DIR "${MySQL_LIBRARY}" DIRECTORY)
    set_tests_properties(mop_world_state_packets PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${MOP_WORLD_STATE_MYSQL_RUNTIME_DIR}")
endif()

add_executable(mop_ai_reaction_packets_test mop_ai_reaction_packets_test.cpp)
target_include_directories(mop_ai_reaction_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_ai_reaction_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_ai_reaction_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_ai_reaction_packets_test PRIVATE shared)
add_test(NAME mop_ai_reaction_packets COMMAND mop_ai_reaction_packets_test)

add_executable(mop_power_update_packets_test mop_power_update_packets_test.cpp)
target_include_directories(mop_power_update_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_power_update_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_power_update_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_power_update_packets_test PRIVATE shared)
add_test(NAME mop_power_update_packets COMMAND mop_power_update_packets_test)

add_executable(mop_sound_packets_test mop_sound_packets_test.cpp)
target_include_directories(mop_sound_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_sound_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_sound_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_sound_packets_test PRIVATE shared)
add_test(NAME mop_sound_packets COMMAND mop_sound_packets_test)

add_executable(mop_pet_action_sound_packets_test mop_pet_action_sound_packets_test.cpp)
target_include_directories(mop_pet_action_sound_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_pet_action_sound_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_pet_action_sound_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_pet_action_sound_packets_test PRIVATE shared)
add_test(NAME mop_pet_action_sound_packets COMMAND mop_pet_action_sound_packets_test)

add_executable(mop_pet_action_feedback_packets_test mop_pet_action_feedback_packets_test.cpp)
target_include_directories(mop_pet_action_feedback_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_pet_action_feedback_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_pet_action_feedback_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_pet_action_feedback_packets_test PRIVATE shared)
add_test(NAME mop_pet_action_feedback_packets COMMAND mop_pet_action_feedback_packets_test)

add_executable(mop_pet_mode_packets_test mop_pet_mode_packets_test.cpp)
target_include_directories(mop_pet_mode_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_pet_mode_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_pet_mode_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_pet_mode_packets_test PRIVATE shared)
add_test(NAME mop_pet_mode_packets COMMAND mop_pet_mode_packets_test)

add_executable(mop_pet_spells_packets_test mop_pet_spells_packets_test.cpp)
target_include_directories(mop_pet_spells_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared
    $<TARGET_PROPERTY:game,INTERFACE_INCLUDE_DIRECTORIES>)
set_target_properties(mop_pet_spells_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(mop_pet_spells_packets_test PRIVATE
    $<TARGET_PROPERTY:game,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(mop_pet_spells_packets_test PRIVATE shared)
add_test(NAME mop_pet_spells_packets COMMAND mop_pet_spells_packets_test)

add_executable(mop_far_sight_packets_test mop_far_sight_packets_test.cpp)
target_include_directories(mop_far_sight_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_far_sight_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_far_sight_packets_test PRIVATE shared)
add_test(NAME mop_far_sight_packets COMMAND mop_far_sight_packets_test)

add_executable(mop_showing_helm_packets_test mop_showing_helm_packets_test.cpp)
target_include_directories(mop_showing_helm_packets_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/shared)
set_target_properties(mop_showing_helm_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(mop_showing_helm_packets_test PRIVATE shared)
add_test(NAME mop_showing_helm_packets COMMAND mop_showing_helm_packets_test)
