[Variables]
VidMemAdapter=1
TotalVM=1

SMLs.FreeVideoMemoryPer.Show=0
SMLs.FreeVideoMemoryPer.RSKN=
SMLs.FreeVideoMemoryPer.Sbtt=
SMLs.FreeVideoMemoryPer.Sufx=

SMLs.UsedVideoMemoryPer.Show=0
SMLs.UsedVideoMemoryPer.RSKN=
SMLs.UsedVideoMemoryPer.Sbtt=
SMLs.UsedVideoMemoryPer.Sufx=

[Ms.Registry.FreeVideoMemory]
Measure=Calc
Formula=Max(0,(Ms.Registry.TotalVideoMemory-Ms.Registry.UsedVideoMemory))
DynamicVariables=1
[Ms.Registry.UsedVideoMemory]
Measure=Plugin
Plugin=UsageMonitor
Alias=VRAM
MinValue=0
[Ms.Registry.TotalVideoMemory]
Measure=Registry
RegHKey=HKEY_LOCAL_MACHINE
RegKey=SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\000#VidMemAdapter#
RegValue=HardwareInformation.qwMemorySize
DynamicVariables=1
[Ms.Registry.FreeVideoMemoryPer]
Measure=Registry
RegHKey=HKEY_CURRENT_USER
RegKey=Software\FinalWire\AIDA[#Is64bit_[#Is64bit]]\SensorValues
RegValue=#SMLs.FreeVideoMemoryPer.RSKN#
RegExpSubstitute=1
Substitute=#SMLs.FreeVideoMemoryPer.Sbtt#
Paused=(1-#SMLs.FreeVideoMemoryPer.Show#)
Disabled=(1-#SMLs.FreeVideoMemoryPer.Show#)
DynamicVariables=1

[Ms.Registry.UsedVideoMemoryPer]
Measure=Registry
RegHKey=HKEY_CURRENT_USER
RegKey=Software\FinalWire\AIDA[#Is64bit_[#Is64bit]]\SensorValues
RegValue=#SMLs.UsedVideoMemoryPer.RSKN#
RegExpSubstitute=1
Substitute=#SMLs.UsedVideoMemoryPer.Sbtt#
Paused=(1-#SMLs.UsedVideoMemoryPer.Show#)
Disabled=(1-#SMLs.UsedVideoMemoryPer.Show#)
DynamicVariables=1

[Ms.Calc.Free]
Measure=Calc
Formula=Ms.Registry.FreeVideoMemory
DynamicVariables=1

[Ms.Calc.Used]
Measure=Calc
Formula=Ms.Registry.UsedVideoMemory
DynamicVariables=1

[Ms.Calc.Total_0]
Measure=Calc
Formula=([Ms.Registry.TotalVideoMemory]*1024**#Unit#)
IfCondition=([Ms.Calc.Total_0]<>#TotalVM#)
IfTrueAction=[!SetVariable "TotalVM" "[Ms.Calc.Total_0]"]
DynamicVariables=1
Paused=(#OperationMethod#<>0)
Disabled=(#OperationMethod#<>0)
[Ms.Calc.Total_1]
Measure=Calc
Formula=Ms.Registry.TotalVideoMemory
IfCondition=([Ms.Calc.Total_1]<>#TotalVM#)
IfTrueAction=[!SetVariable "TotalVM" "[Ms.Calc.Total_1]"]
DynamicVariables=1
Paused=(#OperationMethod#<>1)
Disabled=(#OperationMethod#<>1)
[Ms.Calc.Total_2]
Measure=Calc
Formula=([Ms.Calc.Free]/([Ms.Registry.UsedVideoMemoryPer]/100))
IfCondition=([Ms.Calc.Total_2]<>#TotalVM#)
IfTrueAction=[!SetVariable "TotalVM" "[Ms.Calc.Total_2]"]
DynamicVariables=1
Paused=(#OperationMethod#<>2)
Disabled=(#OperationMethod#<>2)
[Ms.Calc.Total_3]
Measure=Calc
Formula=([Ms.Calc.Used]/([Ms.Registry.FreeVideoMemoryPer]/100))
IfCondition=([Ms.Calc.Total_3]<>#TotalVM#)
IfTrueAction=[!SetVariable "TotalVM" "[Ms.Calc.Total_3]"]
DynamicVariables=1
Paused=(#OperationMethod#<>3)
Disabled=(#OperationMethod#<>3)
[Ms.Calc.Total_4]
Measure=Calc
Formula=([Ms.Calc.Free]/(1-([Ms.Registry.FreeVideoMemoryPer]/100)))
IfCondition=([Ms.Calc.Total_4]<>#TotalVM#)
IfTrueAction=[!SetVariable "TotalVM" "[Ms.Calc.Total_4]"]
DynamicVariables=1
Paused=(#OperationMethod#<>4)
Disabled=(#OperationMethod#<>4)
[Ms.Calc.Total_5]
Measure=Calc
Formula=([Ms.Calc.Used]/(1-([Ms.Registry.UsedVideoMemoryPer]/100)))
IfCondition=([Ms.Calc.Total_5]<>#TotalVM#)
IfTrueAction=[!SetVariable "TotalVM" "[Ms.Calc.Total_5]"]
DynamicVariables=1
Paused=(#OperationMethod#<>5)
Disabled=(#OperationMethod#<>5)

[Ms.Calc.Percentage]
Measure=Calc
Formula=((Ms.Registry.TotalVideoMemory>0)?(([Ms.Calc.Used]/[&Ms.Calc.Total_[#OperationMethod]])*100):0)
MinValue=0
MaxValue=100
DynamicVariables=1