/* Fails the link when less than XRDAP_MIN_FREE_RAM bytes of SRAM are left between the
 * end of the static data (.bss/.noinit, where the heap starts) and the top of SRAM (where
 * the stack starts). XRDAP_MIN_FREE_RAM comes from --defsym (CMakeLists.txt). */
ASSERT(__StackTop - __heap_start__ >= XRDAP_MIN_FREE_RAM,
       "XR-DAP: not enough SRAM left for heap and stack (XRDAP_MIN_FREE_RAM)")
