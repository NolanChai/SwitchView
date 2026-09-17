# NVIDIA profile API headers

Official NVIDIA/nvapi headers pinned at the commit in REVISION.txt:
https://github.com/NVIDIA/nvapi

The headers are MIT licensed; see License.txt. SwitchView dynamically loads the
installed NVIDIA driver library from System32. No driver binaries are bundled.

The extended setting entry points and optional Smooth Motion setting IDs are
documented in NVIDIA Profile Inspector's upstream source:
https://github.com/Orbmu2k/nvidiaProfileInspector/blob/master/nvidiaProfileInspector/Native/NVAPI/NvapiDrsWrapper.cs
https://github.com/Orbmu2k/nvidiaProfileInspector/blob/master/nvidiaProfileInspector/CustomSettingNames.xml

Extended GetSetting: 0xEA99498D, with an extra NvU32 output flag pointer.
Extended SetSetting: 0x8A2CF5F5, with two extra zero NvU32 arguments.
Smooth Motion: 0xB0D384C0 (0 off, 1 on).
Allowed APIs: 0xB0CC0875 (2 DirectX 11).

Only the named SwitchView capture viewer profile and the calling executable's
association are modified. A conflicting existing association is left untouched.
Changes are staged in the session and saved only after both settings succeed.
Enabling the profile is not evidence that the driver interpolates this workload.
