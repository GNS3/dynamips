# dynamips - unstable code

set ( COMMON "${CMAKE_SOURCE_DIR}/common" )
set ( LOCAL "${CMAKE_CURRENT_SOURCE_DIR}" )
include_directories ( "${CMAKE_CURRENT_SOURCE_DIR}" "${COMMON}" "${CMAKE_CURRENT_BINARY_DIR}" )

# mips64_microcode_dump.inc
set ( _input "${LOCAL}/mips64_microcode" )
set ( _output "${CMAKE_CURRENT_BINARY_DIR}/mips64_microcode_dump.inc" )
add_custom_command (
   OUTPUT ${_output}
   COMMAND ${ROM2C_EXECUTABLE} ARGS ${_input} ${_output} 0xbfc00000
   DEPENDS rom2c ${_input}
   )
add_custom_target ( mips64_microcode_dump_unstable
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
add_custom_target ( ppc32_microcode_dump_unstable
   DEPENDS ${_output}
   )

#-------------------------#
# dynamips: unstable code #
#-------------------------#

if ( NOT BUILD_DYNAMIPS_UNSTABLE )
   return ()
endif ( NOT BUILD_DYNAMIPS_UNSTABLE )

# dynamips_*_unstable
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
   "${LOCAL}/tcb.c" # only present in unstable
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
   "${COMMON}/dev_dec21140.c"
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
   "${COMMON}/fs_fat.c"
   "${COMMON}/fs_mbr.c"
   "${COMMON}/fs_nvram.c"
   "${COMMON}/dev_lxt970a.c"
   )
if ( ENABLE_LINUX_ETH )
   set ( _files ${_files} "${COMMON}/linux_eth.c" )
endif ( ENABLE_LINUX_ETH )
if ( ENABLE_GEN_ETH )
   set ( _files ${_files} "${COMMON}/gen_eth.c" )
endif ( ENABLE_GEN_ETH )
set ( _dependencies ppc32_microcode_dump_unstable mips64_microcode_dump_unstable )

# dynamips_amd64_unstable
if ( "amd64" STREQUAL "${DYNAMIPS_ARCH}" )
add_executable ( dynamips_amd64_unstable
   ${_files}
   "${LOCAL}/mips64_amd64_trans.c"
   "${LOCAL}/ppc32_amd64_trans.c"
   )
add_dependencies ( dynamips_amd64_unstable ${_dependencies} )
target_link_libraries ( dynamips_amd64_unstable ${DYNAMIPS_LIBRARIES} )
maybe_rename_to_dynamips ( dynamips_amd64_unstable )
install_executable ( dynamips_amd64_unstable )
endif ()

# dynamips_x86_unstable
if ( "x86" STREQUAL "${DYNAMIPS_ARCH}" )
add_executable ( dynamips_x86_unstable
   ${_files}
   "${LOCAL}/mips64_x86_trans.c"
   "${LOCAL}/ppc32_x86_trans.c"
   )
add_dependencies ( dynamips_x86_unstable ${_dependencies} )
target_link_libraries ( dynamips_x86_unstable ${DYNAMIPS_LIBRARIES} )
maybe_rename_to_dynamips ( dynamips_x86_unstable )
install_executable ( dynamips_x86_unstable )
endif ()

# dynamips_nojit_unstable
if ( "nojit" STREQUAL "${DYNAMIPS_ARCH}" )
add_executable ( dynamips_nojit_unstable
   ${_files}
   "${LOCAL}/mips64_nojit_trans.c"
   "${COMMON}/ppc32_nojit_trans.c"
   )
add_dependencies ( dynamips_nojit_unstable ${_dependencies} )
target_link_libraries ( dynamips_nojit_unstable ${DYNAMIPS_LIBRARIES} )
maybe_rename_to_dynamips ( dynamips_nojit_unstable )
install_executable ( dynamips_nojit_unstable )
endif ()
