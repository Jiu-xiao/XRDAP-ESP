import os, shutil
d = r'D:\Codex\tmp\keil-xrdap'

def proj():
    return r'''<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">
  <SchemaVersion>2.1</SchemaVersion>
  <Header>### uVision Project, (C) Keil Software</Header>
  <Targets>
    <Target>
      <TargetName>T</TargetName>
      <ToolsetNumber>0x4</ToolsetNumber>
      <ToolsetName>ARM-ADS</ToolsetName>
      <uAC6>0</uAC6>
      <TargetOption>
        <TargetCommonOption>
          <Device>ARMCM4</Device>
          <Vendor>ARM</Vendor>
          <Cpu>IRAM(0x20004000,0x4000) IROM(0x20000000,0x4000) CPUTYPE("Cortex-M4") CLOCK(12000000) ELITTLE</Cpu>
          <FlashUtilSpec></FlashUtilSpec>
          <StartupFile></StartupFile>
          <FlashDriverDll></FlashDriverDll>
          <DeviceId>0</DeviceId>
          <OutputDirectory>.\Objects\</OutputDirectory>
          <OutputName>t</OutputName>
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
        <TargetArmAds>
          <ArmAdsMisc><useUlib>1</useUlib><OptFeed>0</OptFeed></ArmAdsMisc>
          <Cads><interw>1</interw><Optim>1</Optim><oTime>0</oTime><SplitLS>0</SplitLS><OneElfS>0</OneElfS><Strict>0</Strict><EnumInt>0</EnumInt><PlainCh>0</PlainCh><Ropi>0</Ropi><Rwpi>0</Rwpi><wLevel>2</wLevel><uThumb>0</uThumb><uSurpInc>0</uSurpInc><uC99>1</uC99><useXO>0</useXO><VariousControls><MiscControls></MiscControls><Define></Define><Undefine></Undefine><IncludePath></IncludePath></VariousControls></Cads>
          <Aads><interw>1</interw><Ropi>0</Ropi><Rwpi>0</Rwpi><thumb>0</thumb><SplitLS>0</SplitLS><SwStkChk>0</SwStkChk><NoWarn>0</NoWarn><uSurpInc>0</uSurpInc><useXO>0</useXO><VariousControls><MiscControls></MiscControls><Define></Define><Undefine></Undefine><IncludePath></IncludePath></VariousControls></Aads>
          <LDads><umfTarg>0</umfTarg><Ropi>0</Ropi><Rwpi>0</Rwpi><noStLib>0</noStLib><RepFail>1</RepFail><useFile>0</useFile><TextAddressRange>0x20000000</TextAddressRange><DataAddressRange>0x20004000</DataAddressRange><pXoBase></pXoBase><ScatterFile>..\t.sct</ScatterFile><IncludeLibs></IncludeLibs><IncludeLibsPath></IncludeLibsPath><Misc>--entry Reset_Handler</Misc><LinkerInputFile></LinkerInputFile><DisabledWarnings></DisabledWarnings></LDads>
        </TargetArmAds>
      </TargetOption>
      <Groups>
        <Group><GroupName>src</GroupName><Files><File><FileName>main.c</FileName><FileType>1</FileType><FilePath>.\main.c</FilePath></File></Files></Group>
      </Groups>
    </Target>
  </Targets>
</Project>
'''

def opt(clk):
    return r'''<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<ProjectOpt xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_optx.xsd">
  <SchemaVersion>1.0</SchemaVersion>
  <Header>### uVision Project, (C) Keil Software</Header>
  <Target>
    <TargetName>T</TargetName>
    <ToolsetNumber>0x4</ToolsetNumber>
    <ToolsetName>ARM-ADS</ToolsetName>
    <TargetOption>
      <CLKADS>12000000</CLKADS>
      <DebugOpt>
        <uSim>0</uSim><uTrg>1</uTrg><sLdApp>1</sLdApp><sGomain>1</sGomain><sRbreak>1</sRbreak><sRwatch>1</sRwatch><sRmem>1</sRmem><sRfunc>1</sRfunc><sRbox>1</sRbox>
        <tLdApp>1</tLdApp><tGomain>0</tGomain><tRbreak>1</tRbreak><tRwatch>1</tRwatch><tRmem>1</tRmem><tRfunc>0</tRfunc><tRbox>1</tRbox><tRtrace>1</tRtrace>
        <sRSysVw>1</sRSysVw><tRSysVw>1</tRSysVw><sRunDeb>0</sRunDeb><sLrtime>0</sLrtime><bEvRecOn>1</bEvRecOn><bSchkAxf>0</bSchkAxf><bTchkAxf>0</bTchkAxf><nTsel>3</nTsel>
        <sDll></sDll><sDllPa></sDllPa><sDlgDll></sDlgDll><sDlgPa></sDlgPa><sIfile></sIfile>
        <tDll></tDll><tDllPa></tDllPa><tDlgDll></tDlgDll><tDlgPa></tDlgPa><tIfile>D:\Codex\tmp\keil-xrdap\keil.ini</tIfile>
        <pMon>BIN\CMSIS_AGDI.dll</pMon>
      </DebugOpt>
      <TargetDriverDllRegistry>
        <SetRegEntry><Number>0</Number><Key>CMSIS_AGDI</Key><Name>-X"" -O206 -S0 -C0 -P00000000 -N00("ARM CoreSight SW-DP") -D00(2BA01477) -L00(0) -TO65554 -TC%(clk)d -TT%(clk)d -TP20 -TDS8007 -TDT0 -TDC1F -TIEFFFFFFFF -TIP8 -FO7 -FD20004000 -FC1000 -FN0</Name></SetRegEntry>
        <SetRegEntry><Number>0</Number><Key>DLGTARM</Key><Name>(1010=-1,-1,-1,-1,0)(1007=-1,-1,-1,-1,0)(1008=-1,-1,-1,-1,0)(1009=-1,-1,-1,-1,0)(1012=-1,-1,-1,-1,0)</Name></SetRegEntry>
        <SetRegEntry><Number>0</Number><Key>ARMDBGFLAGS</Key><Name></Name></SetRegEntry>
      </TargetDriverDllRegistry>
    </TargetOption>
  </Target>
</ProjectOpt>
''' % {'clk': clk}

for clk in (4000000, 12000000):
    p = f'{d}\\p{clk // 1000000}'
    os.makedirs(p, exist_ok=True)
    open(f'{p}\\t.uvprojx', 'w').write(proj())
    open(f'{p}\\t.uvoptx', 'w').write(opt(clk))
    shutil.copy(f'{d}\\main.c', f'{p}\\main.c')
print('ok')
