# stable

set ( _executables )
set ( COMMON "${CMAKE_SOURCE_DIR}/common" )
set ( LOCAL "${CMAKE_CURRENT_SOURCE_DIR}" )
include_directories ( "${CMAKE_CURRENT_SOURCE_DIR}" "${COMMON}" "${CMAKE_CURRENT_BINARY_DIR}" )

# udp_send
if ( BUILD_UDP_SEND )
add_executable ( udp_send EXCLUDE_FROM_ALL
   "${COMMON}/crc.c"
   "${COMMON}/net.c"
   "${COMMON}/udp_send.c"
   "${COMMON}/utils.c"
   )
install_executables ( udp_send )
endif ( BUILD_UDP_SEND )

# udp_recv
if ( BUILD_UDP_RECV )
add_executable ( udp_recv EXCLUDE_FROM_ALL
   "${COMMON}/crc.c"
   "${COMMON}/net.c"
   "${COMMON}/udp_recv.c"
   "${COMMON}/utils.c"
   )
install_executables ( udp_recv )
endif ( BUILD_UDP_RECV )

# rom2c
add_executable ( rom2c EXCLUDE_FROM_ALL
   "${COMMON}/rom2c.c"
   )
target_link_libraries ( rom2c ${LIBELF_LIBRARIES} )
set ( ROM2C_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/rom2c${CMAKE_EXECUTABLE_SUFFIX}" CACHE INTERNAL "rom2c executable" )

# mips64_microcode_dump.inc
set ( _input "${LOCAL}/mips64_microcode" )
set ( _output "${CMAKE_CURRENT_BINARY_DIR}/mips64_microcode_dump.inc" )
add_custom_command (
   OUTPUT ${_output}
   COMMAND ${ROM2C_EXECUTABLE} ARGS ${_input} ${_output} 0xbfc00000
   DEPENDS rom2c ${_input}
   )
add_custom_target ( mips64_microcode_dump_stable
   DEPENDS ${_output}
   )

# ppc32_microcode_dump.inc
set ( _input "${LOCAL}/ppc32_microcode" )
set ( _output "${CMAKE_CURRENT_BINARY_DIR}/ppc32_microcode_dump.inc" )
add_custom_command (
   OUTPUT ${_output}
   COMMAND ${ROM2C_EXECUTABLE} ARGS ${_input} ${_output} 0xfff00000
   DEPENDS rom2c ${_input}
   )
add_custom_target ( ppc32_microcode_dump_stable
   DEPENDS ${_output}
   )

# nvram_export
if ( BUILD_NVRAM_EXPORT )
add_executable ( nvram_export
   "${COMMON}/fs_nvram.c"
   "${COMMON}/nvram_export.c"
   )
install_executables ( nvram_export )
endif ( BUILD_NVRAM_EXPORT )

#-----------------------#
# dynamips: stable code #
#-----------------------#

if ( NOT BUILD_DYNAMIPS_STABLE )
   return ()
endif ( NOT BUILD_DYNAMIPS_STABLE )

# dynamips_*_stable
set ( _files
   "${COMMON}/mempool.c"
   "${COMMON}/registry.c"
   "${COMMON}/rbtree.c"
   "${COMMON}/hash.c"
   "${COMMON}/sbox.c"
   "${COMMON}/utils.c"
   "${COMMON}/parser.c"
   "${COMMON}/gen_uuid.c"
   "${COMMON}/plugin.c"
   "${COMMON}/ptask.c"
   "${COMMON}/timer.c"
   "${COMMON}/crc.c"
   "${COMMON}/base64.c"
   "${COMMON}/net.c"
   "${COMMON}/net_io.c"
   "${COMMON}/net_io_bridge.c"
   "${COMMON}/net_io_filter.c"
   "${COMMON}/atm.c"
   "${COMMON}/atm_vsar.c"
   "${COMMON}/atm_bridge.c"
   "${COMMON}/frame_relay.c"
   "${COMMON}/eth_switch.c"
   "${COMMON}/dynamips.c"
   "${COMMON}/insn_lookup.c"
   "${LOCAL}/vm.c"
   "${LOCAL}/cpu.c"
   "${COMMON}/jit_op.c"
   "${LOCAL}/mips64.c"
   "${LOCAL}/mips64_mem.c"
   "${LOCAL}/mips64_cp0.c"
   "${LOCAL}/mips64_jit.c"
   "${LOCAL}/mips64_exec.c"
   "${LOCAL}/ppc32.c"
   "${LOCAL}/ppc32_mem.c"
   "${LOCAL}/ppc32_jit.c"
   "${LOCAL}/ppc32_exec.c"
   "${LOCAL}/ppc32_vmtest.c"
   "${COMMON}/memory.c"
   "${COMMON}/device.c"
   "${COMMON}/nmc93cX6.c"
   "${COMMON}/cisco_eeprom.c"
   "${COMMON}/cisco_card.c"
   "${COMMON}/pci_dev.c"
   "${COMMON}/pci_io.c"
   "${COMMON}/dev_zero.c"
   "${COMMON}/dev_bswap.c"
   "${COMMON}/dev_vtty.c"
   "${COMMON}/dev_ram.c"
   "${COMMON}/dev_rom.c"
   "${COMMON}/dev_nvram.c"
   "${COMMON}/dev_bootflash.c"
   "${COMMON}/dev_flash.c"
   "${COMMON}/dev_mpc860.c"
   "${COMMON}/dev_ds1620.c"
   "${COMMON}/dev_remote.c"
   "${COMMON}/dev_clpd6729.c"
   "${COMMON}/dev_pcmcia_disk.c"
   "${COMMON}/dev_gt.c"
   "${COMMON}/dev_mv64460.c"
   "${COMMON}/dev_plx.c"
   "${COMMON}/dev_dec21x50.c"
   "${COMMON}/dev_pericom.c"
   "${COMMON}/dev_ti2050b.c"
   "${COMMON}/dev_ap1011.c"
   "${COMMON}/dev_plx6520cb.c"
   "${COMMON}/dev_ns16552.c"
   "${LOCAL}/dev_dec21140.c"
   "${COMMON}/dev_am79c971.c"
   "${COMMON}/dev_i8254x.c"
   "${COMMON}/dev_i8255x.c"
   "${COMMON}/dev_mueslix.c"
   "${COMMON}/dev_wic_serial.c"
   "${COMMON}/dev_c3600.c"
   "${COMMON}/dev_c3600_bay.c"
   "${COMMON}/dev_c3600_iofpga.c"
   "${COMMON}/dev_c3600_eth.c"
   "${COMMON}/dev_c3600_serial.c"
   "${COMMON}/dev_c7200.c"
   "${COMMON}/dev_c7200_iofpga.c"
   "${COMMON}/dev_c7200_mpfpga.c"
   "${COMMON}/dev_c7200_sram.c"
   "${COMMON}/dev_c7200_eth.c"
   "${COMMON}/dev_c7200_serial.c"
   "${COMMON}/dev_c7200_pos.c"
   "${COMMON}/dev_c7200_bri.c"
   "${COMMON}/dev_c7200_jcpa.c"
   "${COMMON}/dev_c2691.c"
   "${COMMON}/dev_c2691_iofpga.c"
   "${COMMON}/dev_c2691_eth.c"
   "${COMMON}/dev_c2691_serial.c"
   "${COMMON}/dev_c2691_wic.c"
   "${COMMON}/dev_c2691_pcmod.c"
   "${COMMON}/dev_c3725.c"
   "${COMMON}/dev_c3725_iofpga.c"
   "${COMMON}/dev_c3725_eth.c"
   "${COMMON}/dev_c3725_serial.c"
   "${COMMON}/dev_c3725_wic.c"
   "${COMMON}/dev_c3725_pcmod.c"
   "${COMMON}/dev_c3745.c"
   "${COMMON}/dev_c3745_iofpga.c"
   "${COMMON}/dev_c3745_eth.c"
   "${COMMON}/dev_c3745_serial.c"
   "${COMMON}/dev_c3745_wic.c"
   "${COMMON}/dev_c3745_pcmod.c"
   "${COMMON}/dev_c2600.c"
   "${COMMON}/dev_c2600_pci.c"
   "${COMMON}/dev_c2600_iofpga.c"
   "${COMMON}/dev_c2600_eth.c"
   "${COMMON}/dev_c2600_pcmod.c"
   "${COMMON}/dev_c2600_wic.c"
   "${COMMON}/dev_c1700.c"
   "${COMMON}/dev_c1700_iofpga.c"
   "${COMMON}/dev_c1700_eth.c"
   "${COMMON}/dev_c1700_wic.c"
   "${COMMON}/dev_c6msfc1.c"
   "${COMMON}/dev_c6msfc1_iofpga.c"
   "${COMMON}/dev_c6msfc1_mpfpga.c"
   "${COMMON}/dev_c6sup1.c"
   "${COMMON}/dev_c6sup1_iofpga.c"
   "${COMMON}/dev_c6sup1_mpfpga.c"
   "${COMMON}/dev_nm_16esw.c"
   "${COMMON}/dev_pa_a1.c"
   "${COMMON}/dev_pa_mc8te1.c"
   "${COMMON}/dev_sb1.c"
   "${COMMON}/dev_sb1_io.c"
   "${COMMON}/dev_sb1_pci.c"
   "${LOCAL}/hypervisor.c"
   "${COMMON}/hv_nio.c"
   "${COMMON}/hv_nio_bridge.c"
   "${COMMON}/hv_frsw.c"
   "${COMMON}/hv_atmsw.c"
   "${COMMON}/hv_atm_bridge.c"
   "${COMMON}/hv_ethsw.c"
   "${LOCAL}/hv_vm.c"
   "${COMMON}/hv_vm_debug.c"
   "${COMMON}/hv_store.c"
   "${COMMON}/hv_c7200.c"
   "${COMMON}/hv_c3600.c"
   "${COMMON}/hv_c2691.c"
   "${COMMON}/hv_c3725.c"
   "${COMMON}/hv_c3745.c"
   "${COMMON}/hv_c2600.c"
   "${COMMON}/hv_c1700.c"
   "${COMMON}/rommon_var.c"
   "${COMMON}/get_cpu_time.c"
   "${COMMON}/fs_nvram.c"
   "${COMMON}/dev_lxt970a.c"
   )
set ( _depends ppc32_microcode_dump_stable mips64_microcode_dump_stable )
set ( _flags ${DYNAMIPS_FLAGS} )
set ( _libraries ${DYNAMIPS_LIBRARIES} )
if ( ENABLE_LINUX_ETH )
   set ( _files ${_files} "${COMMON}/linux_eth.c" )
   set ( _flags ${_flags} -DLINUX_ETH )
endif ( ENABLE_LINUX_ETH )
if ( ENABLE_GEN_ETH )
   set ( _files ${_files} "${COMMON}/gen_eth.c" )
   set ( _flags ${_flags} -DGEN_ETH )
   set ( _libraries ${_libraries} ${PCAP_LIBRARIES} )
endif ( ENABLE_GEN_ETH )

# dynamips_x86_stable
if ( "${DYNAMIPS_ARCH}" STREQUAL "x86" )
add_executable ( dynamips_x86_stable
   ${_files}
   "${LOCAL}/mips64_x86_trans.c"
   "${LOCAL}/ppc32_x86_trans.c"
   )
add_dependencies ( dynamips_x86_stable ${_depends} )
target_add_compile_flags ( dynamips_x86_stable ${_flags} -m32 )
target_link_libraries ( dynamips_x86_stable ${_libraries} )
install_executables ( dynamips_x86_stable )
endif ( "${DYNAMIPS_ARCH}" STREQUAL "x86" )

# dynamips_amd64_stable
if ( "${DYNAMIPS_ARCH}" STREQUAL "amd64" )
add_executable ( dynamips_amd64_stable
   ${_files}
   "${LOCAL}/mips64_amd64_trans.c"
   "${LOCAL}/ppc32_amd64_trans.c"
   )
add_dependencies ( dynamips_amd64_stable ${_depends} )
target_add_compile_flags ( dynamips_amd64_stable ${_flags} -m64 )
if ( APPLE )
   target_add_compile_flags ( dynamips_amd64_stable -DMAC64HACK )
endif ( APPLE )
target_link_libraries ( dynamips_amd64_stable ${_libraries} )
install_executables ( dynamips_amd64_stable )
endif ( "${DYNAMIPS_ARCH}" STREQUAL "amd64" )

# dynamips_nojit_stable
if ( "${DYNAMIPS_ARCH}" STREQUAL "nojit" )
add_executable ( dynamips_nojit_stable
   ${_files}
   "${COMMON}/mips64_nojit_trans.c"
   "${COMMON}/ppc32_nojit_trans.c"
   )
add_dependencies ( dynamips_nojit_stable ${_depends} )
target_add_compile_flags ( dynamips_nojit_stable ${_flags} )
target_link_libraries ( dynamips_nojit_stable ${_libraries} )
install_executables ( dynamips_nojit_stable )
endif ( "${DYNAMIPS_ARCH}" STREQUAL "nojit" )
