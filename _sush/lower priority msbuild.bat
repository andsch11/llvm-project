wmic process where name="msbuild.exe" CALL setpriority "Below Normal"
wmic process where name="cl.exe" CALL setpriority "Below Normal"
wmic process where name="link.exe" CALL setpriority "Below Normal"