import os
d = r'D:\Codex\tmp\keil-xrdap\g431'
os.makedirs(d, exist_ok=True)

import random
random.seed(431)
words = [random.getrandbits(32) for _ in range(24576)]   # 96 KiB of pseudo-random data
blob = 'const uint32_t blob[24576] __attribute__((used)) = {\n' + ',\n'.join(
    ', '.join('0x%08Xu' % w for w in words[i:i + 8]) for i in range(0, len(words), 8)) + '\n};\n'
open(d + r'\main.c', 'w').write('#include <stdint.h>\nvolatile uint32_t counter;\nvolatile uint32_t flag = 0x12345678u;\n'
    + blob + '''void Reset_Handler(void) { for (;;) { counter++; flag ^= counter ^ blob[counter & 0x3FFF]; } }
const uint32_t vectors[2] __attribute__((section("RESET"))) = { 0x20008000u, (uint32_t)Reset_Handler };
''')
open(d + r'\t.sct', 'w').write('''LR_IROM1 0x08000000 0x20000 {
  ER_IROM1 0x08000000 0x20000 {
    *.o (RESET, +First)
    .ANY (+RO)
  }
  RW_IRAM1 0x20000000 0x8000 {
    .ANY (+RW +ZI)
  }
}
''')
# freeze the target IWDG/WWDG while debugging (DBGMCU_APB1FZR1)
open(d + r'\dbg.ini', 'w').write('''FUNC void Setup (void) {
  _WDWORD(0xE0042008, 0x1800);
}
Setup();
''')

CLK = 4000000
proj = r'''<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">
  <SchemaVersion>2.1</SchemaVersion>
  <Header>### uVision Project, (C) Keil Software</Header>
  <Targets>
    <Target>
      <TargetName>G431</TargetName>
      <ToolsetNumber>0x4</ToolsetNumber>
      <ToolsetName>ARM-ADS</ToolsetName>
      <uAC6>0</uAC6>
      <TargetOption>
        <TargetCommonOption>
          <Device>STM32G431KBTx</Device>
          <Vendor>STMicroelectronics</Vendor>
          <PackID>Keil.STM32G4xx_DFP.2.2.0</PackID>
          <PackURL>https://www.keil.com/pack/</PackURL>
          <Cpu>IRAM(0x20000000,0x8000) IROM(0x08000000,0x20000) CPUTYPE("Cortex-M4") CLOCK(8000000) ELITTLE</Cpu>
          <FlashUtilSpec></FlashUtilSpec>
          <StartupFile></StartupFile>
          <FlashDriverDll>UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0STM32G43x-4x_128 -FS08000000 -FL020000 -FP0($$Device:STM32G431KBTx$CMSIS\Flash\STM32G43x-4x_128.FLM))</FlashDriverDll>
          <DeviceId>0</DeviceId>
          <OutputDirectory>.\Objects\</OutputDirectory>
          <OutputName>g431</OutputName>
          <CreateExecutable>1</CreateExecutable>
          <CreateHexFile>0</CreateHexFile>
          <DebugInformation>1</DebugInformation>
          <ListingPath>.\Listings\</ListingPath>
        </TargetCommonOption>
        <CommonProperty>
          <UseCPPCompiler>0</UseCPPCompiler><RVCTCodeConst>0</RVCTCodeConst><RVCTZI>0</RVCTZI><RVCTOtherData>0</RVCTOtherData>
          <ModuleSelection>0</ModuleSelection><IncludeInBuild>1</IncludeInBuild><AlwaysBuild>0</AlwaysBuild>
          <GenerateAssemblyFile>0</GenerateAssemblyFile><AssembleAssemblyFile>0</AssembleAssemblyFile><PublicsOnly>0</PublicsOnly>
          <StopOnExitCode>3</StopOnExitCode><CustomArgument></CustomArgument><IncludeLibraryModules></IncludeLibraryModules>
          <ComprImg>1</ComprImg>
        </CommonProperty>
        <DllOption>
          <SimDllName>SARMCM3.DLL</SimDllName><SimDllArguments> -REMAP</SimDllArguments><SimDlgDll>DCM.DLL</SimDlgDll><SimDlgDllArguments>-pCM4</SimDlgDllArguments>
          <TargetDllName>SARMCM3.DLL</TargetDllName><TargetDllArguments> -MPU</TargetDllArguments><TargetDlgDll>TCM.DLL</TargetDlgDll><TargetDlgDllArguments>-pCM4</TargetDlgDllArguments>
        </DllOption>
        <DebugOption><OPTHX><HexSelection>1</HexSelection><HexRangeLowAddress>0</HexRangeLowAddress><HexRangeHighAddress>0</HexRangeHighAddress><HexOffset>0</HexOffset><Oh166RecLen>16</Oh166RecLen></OPTHX></DebugOption>
        <Utilities>
          <Flash1><UseTargetDll>1</UseTargetDll><UseExternalTool>0</UseExternalTool><RunIndependent>0</RunIndependent><UpdateFlashBeforeDebugging>1</UpdateFlashBeforeDebugging><Capability>1</Capability><DriverSelection>4096</DriverSelection></Flash1>
          <bUseTDR>1</bUseTDR>
          <Flash2>BIN\CMSIS_AGDI.dll</Flash2>
          <Flash3></Flash3><Flash4></Flash4><pFcarmOut></pFcarmOut><pFcarmGrp></pFcarmGrp><pFcArmRoot></pFcArmRoot><FcArmLst>0</FcArmLst>
        </Utilities>
        <TargetArmAds>
          <ArmAdsMisc><useUlib>1</useUlib><OptFeed>0</OptFeed></ArmAdsMisc>
          <Cads><interw>1</interw><Optim>1</Optim><oTime>0</oTime><SplitLS>0</SplitLS><OneElfS>0</OneElfS><Strict>0</Strict><EnumInt>0</EnumInt><PlainCh>0</PlainCh><Ropi>0</Ropi><Rwpi>0</Rwpi><wLevel>2</wLevel><uThumb>0</uThumb><uSurpInc>0</uSurpInc><uC99>1</uC99><useXO>0</useXO><VariousControls><MiscControls></MiscControls><Define></Define><Undefine></Undefine><IncludePath></IncludePath></VariousControls></Cads>
          <Aads><interw>1</interw><Ropi>0</Ropi><Rwpi>0</Rwpi><thumb>0</thumb><SplitLS>0</SplitLS><SwStkChk>0</SwStkChk><NoWarn>0</NoWarn><uSurpInc>0</uSurpInc><useXO>0</useXO><VariousControls><MiscControls></MiscControls><Define></Define><Undefine></Undefine><IncludePath></IncludePath></VariousControls></Aads>
          <LDads><umfTarg>0</umfTarg><Ropi>0</Ropi><Rwpi>0</Rwpi><noStLib>0</noStLib><RepFail>1</RepFail><useFile>0</useFile><TextAddressRange>0x08000000</TextAddressRange><DataAddressRange>0x20000000</DataAddressRange><pXoBase></pXoBase><ScatterFile>.\t.sct</ScatterFile><IncludeLibs></IncludeLibs><IncludeLibsPath></IncludeLibsPath><Misc>--entry Reset_Handler</Misc><LinkerInputFile></LinkerInputFile><DisabledWarnings></DisabledWarnings></LDads>
        </TargetArmAds>
      </TargetOption>
      <Groups>
        <Group><GroupName>src</GroupName><Files><File><FileName>main.c</FileName><FileType>1</FileType><FilePath>.\main.c</FilePath></File></Files></Group>
      </Groups>
    </Target>
  </Targets>
</Project>
'''
opt = r'''<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<ProjectOpt xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_optx.xsd">
  <SchemaVersion>1.0</SchemaVersion>
  <Header>### uVision Project, (C) Keil Software</Header>
  <Target>
    <TargetName>G431</TargetName>
    <ToolsetNumber>0x4</ToolsetNumber>
    <ToolsetName>ARM-ADS</ToolsetName>
    <TargetOption>
      <CLKADS>8000000</CLKADS>
      <DebugOpt>
        <uSim>0</uSim><uTrg>1</uTrg><sLdApp>1</sLdApp><sGomain>1</sGomain><sRbreak>1</sRbreak><sRwatch>1</sRwatch><sRmem>1</sRmem><sRfunc>1</sRfunc><sRbox>1</sRbox>
        <tLdApp>1</tLdApp><tGomain>0</tGomain><tRbreak>1</tRbreak><tRwatch>1</tRwatch><tRmem>1</tRmem><tRfunc>0</tRfunc><tRbox>1</tRbox><tRtrace>1</tRtrace>
        <sRSysVw>1</sRSysVw><tRSysVw>1</tRSysVw><sRunDeb>0</sRunDeb><sLrtime>0</sLrtime><bEvRecOn>1</bEvRecOn><bSchkAxf>0</bSchkAxf><bTchkAxf>0</bTchkAxf><nTsel>3</nTsel>
        <sDll></sDll><sDllPa></sDllPa><sDlgDll></sDlgDll><sDlgPa></sDlgPa><sIfile></sIfile>
        <tDll></tDll><tDllPa></tDllPa><tDlgDll></tDlgDll><tDlgPa></tDlgPa><tIfile>.\dbg.ini</tIfile>
        <pMon>BIN\CMSIS_AGDI.dll</pMon>
      </DebugOpt>
      <TargetDriverDllRegistry>
        <SetRegEntry><Number>0</Number><Key>CMSIS_AGDI</Key><Name>-X"" -O206 -S0 -C0 -P00000000 -N00("ARM CoreSight SW-DP") -D00(2BA01477) -L00(0) -TO65554 -TC%(clk)d -TT%(clk)d -TP20 -TDS8007 -TDT0 -TDC1F -TIEFFFFFFFF -TIP8 -FO7 -FD20000000 -FC1000 -FN1 -FF0STM32G43x-4x_128 -FS08000000 -FL020000 -FP0($$Device:STM32G431KBTx$CMSIS\Flash\STM32G43x-4x_128.FLM)</Name></SetRegEntry>
        <SetRegEntry><Number>0</Number><Key>UL2CM3</Key><Name>UL2CM3(-S0 -C0 -P0 )  -FN1 -FC1000 -FD20000000 -FF0STM32G43x-4x_128 -FL020000 -FS08000000 -FP0($$Device:STM32G431KBTx$CMSIS\Flash\STM32G43x-4x_128.FLM)</Name></SetRegEntry>
        <SetRegEntry><Number>0</Number><Key>DLGTARM</Key><Name>(1010=-1,-1,-1,-1,0)(1007=-1,-1,-1,-1,0)(1008=-1,-1,-1,-1,0)(1009=-1,-1,-1,-1,0)(1012=-1,-1,-1,-1,0)</Name></SetRegEntry>
        <SetRegEntry><Number>0</Number><Key>ARMDBGFLAGS</Key><Name></Name></SetRegEntry>
      </TargetDriverDllRegistry>
    </TargetOption>
  </Target>
</ProjectOpt>
''' % {'clk': CLK}
open(d + r'\g431.uvprojx', 'w').write(proj)
open(d + r'\g431.uvoptx', 'w').write(opt)
print('ok')
