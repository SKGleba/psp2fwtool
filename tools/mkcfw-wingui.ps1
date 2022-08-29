Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

#get path
$scriptDir = if (-not $PSScriptRoot) { Split-Path -Parent (Convert-Path ([environment]::GetCommandLineArgs()[0])) } else { $PSScriptRoot }

$form = New-Object System.Windows.Forms.Form
$form.Text = 'cfwimg-gui'
$form.Size = New-Object System.Drawing.Size(450, 200)
$form.StartPosition = 'CenterScreen'

# file name
$fname_label = New-Object System.Windows.Forms.Label
$fname_label.Location = New-Object System.Drawing.Point(10, 12)
$fname_label.Text = 'file name:'
$fname_label.AutoSize = $true
$fname_label.Font = 'Microsoft Sans Serif,10,style=Bold'
$form.Controls.Add($fname_label)

$fnameBox = New-Object System.Windows.Forms.TextBox
$fnameBox.Location = New-Object System.Drawing.Point(85, 10)
$fnameBox.Size = New-Object System.Drawing.Size(80, 20)
$fnameBox.Text = 'psp2cfw'
$form.Controls.Add($fnameBox)

$file_type_label = New-Object System.Windows.Forms.Label
$file_type_label.Location = New-Object System.Drawing.Point(170, 10)
$file_type_label.Text = '?'
$file_type_label.AutoSize = $true
$file_type_label.Font = 'Microsoft Sans Serif,10,style=Bold'
$form.Controls.Add($file_type_label)

$gp_cbox = New-Object System.Windows.Forms.Checkbox 
$gp_cbox.Location = New-Object System.Drawing.Point(193, 10) 
$gp_cbox.AutoSize = $true
$gp_cbox.Text = "PUP"
$gp_cbox.Font = 'Microsoft Sans Serif,10,style=Bold'
$gp_cbox.TabIndex = 4
$form.Controls.Add($gp_cbox)

# fw version
$fw_label = New-Object System.Windows.Forms.Label
$fw_label.Location = New-Object System.Drawing.Point(10, 42)
$fw_label.Text = 'firmware:'
$fw_label.AutoSize = $true
$fw_label.Font = 'Microsoft Sans Serif,10,style=Bold'
$form.Controls.Add($fw_label)

$fwBox = New-Object System.Windows.Forms.TextBox
$fwBox.Location = New-Object System.Drawing.Point(85, 40)
$fwBox.Size = New-Object System.Drawing.Size(80, 20)
$fwBox.Text = '0x03650000'
$form.Controls.Add($fwBox)

$fw_type_label = New-Object System.Windows.Forms.Label
$fw_type_label.Location = New-Object System.Drawing.Point(167, 40)
$fw_type_label.Text = '@'
$fw_type_label.AutoSize = $true
$fw_type_label.Font = 'Microsoft Sans Serif,10,style=Bold'
$form.Controls.Add($fw_type_label)

$fwType = New-Object system.Windows.Forms.ComboBox
$fwType.width = 80
$fwType.autosize = $true
@('SysDbg', 'DevKit', 'TestKit', 'Retail', 'QA', 'ALL', 'Soft (safe)') | ForEach-Object { [void] $fwType.Items.Add($_) }
$fwType.location = New-Object System.Drawing.Point(193, 40)
$fwType.Text = 'select target'
$form.Controls.Add($fwType)

# hw version
$hw_rev_label = New-Object System.Windows.Forms.Label
$hw_rev_label.Location = New-Object System.Drawing.Point(10, 71)
$hw_rev_label.Text = 'hardware:'
$hw_rev_label.AutoSize = $true
$hw_rev_label.Font = 'Microsoft Sans Serif,10,style=Bold'
$form.Controls.Add($hw_rev_label)

$hw_rev_Box = New-Object System.Windows.Forms.TextBox
$hw_rev_Box.Location = New-Object System.Drawing.Point(85, 70)
$hw_rev_Box.Size = New-Object System.Drawing.Size(80, 20)
$hw_rev_Box.Text = '0x00000000'
$form.Controls.Add($hw_rev_Box)

$hw_mask_label = New-Object System.Windows.Forms.Label
$hw_mask_label.Location = New-Object System.Drawing.Point(170, 71)
$hw_mask_label.Text = '&&'
$hw_mask_label.AutoSize = $true
$hw_mask_label.Font = 'Microsoft Sans Serif,10,style=Bold'
$form.Controls.Add($hw_mask_label)

$hw_mask_Box = New-Object System.Windows.Forms.TextBox
$hw_mask_Box.Location = New-Object System.Drawing.Point(193, 70)
$hw_mask_Box.Size = New-Object System.Drawing.Size(80, 20)
$hw_mask_Box.Text = '0x00000000'
$form.Controls.Add($hw_mask_Box)

# build info
$info_label = New-Object System.Windows.Forms.Label
$info_label.Location = New-Object System.Drawing.Point(10, 101)
$info_label.Text = 'build info:'
$info_label.AutoSize = $true
$info_label.Font = 'Microsoft Sans Serif,10,style=Bold'
$form.Controls.Add($info_label)

$info_Box = New-Object System.Windows.Forms.TextBox
$info_Box.Location = New-Object System.Drawing.Point(85, 100)
$info_Box.Size = New-Object System.Drawing.Size(187, 20)
$info_Box.Text = '"my own special custom firmware"'
$form.Controls.Add($info_Box)

# buttons
$okButton = New-Object System.Windows.Forms.Button
$okButton.Location = New-Object System.Drawing.Point(15, 130)
$okButton.Size = New-Object System.Drawing.Size(75, 23)
$okButton.Text = 'CREATE'
$okButton.DialogResult = [System.Windows.Forms.DialogResult]::OK
$form.AcceptButton = $okButton
$form.Controls.Add($okButton)

$infoButton = New-Object System.Windows.Forms.Button
$infoButton.Location = New-Object System.Drawing.Point(100, 130)
$infoButton.Size = New-Object System.Drawing.Size(75, 23)
$infoButton.Text = 'INFO'
$infoButton.DialogResult = [System.Windows.Forms.DialogResult]::Yes
$form.CancelButton = $infoButton
$form.Controls.Add($infoButton)

$cancelButton = New-Object System.Windows.Forms.Button
$cancelButton.Location = New-Object System.Drawing.Point(185, 130)
$cancelButton.Size = New-Object System.Drawing.Size(75, 23)
$cancelButton.Text = 'EXIT'
$cancelButton.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$form.CancelButton = $cancelButton
$form.Controls.Add($cancelButton)

# Images
$enso_label = New-Object System.Windows.Forms.Label
$enso_label.Location = New-Object System.Drawing.Point(285, 10)
$enso_label.Text = '*.bin'
$enso_label.AutoSize = $true
$enso_label.Font = 'Microsoft Sans Serif,7,style=Bold'
$form.Controls.Add($enso_label)

$enso_cbox = New-Object System.Windows.Forms.Checkbox 
$enso_cbox.Location = New-Object System.Drawing.Point(290, 22) 
$enso_cbox.AutoSize = $true
$enso_cbox.Text = "enso"
$form.Controls.Add($enso_cbox)

$img_label = New-Object System.Windows.Forms.Label
$img_label.Location = New-Object System.Drawing.Point(285, 40)
$img_label.Text = '*.img'
$img_label.AutoSize = $true
$img_label.Font = 'Microsoft Sans Serif,7,style=Bold'
$form.Controls.Add($img_label)

$slb2_cbox = New-Object System.Windows.Forms.Checkbox 
$slb2_cbox.Location = New-Object System.Drawing.Point(290, 53) 
$slb2_cbox.AutoSize = $true
$slb2_cbox.Text = "slb2"
$form.Controls.Add($slb2_cbox)

$os0_cbox = New-Object System.Windows.Forms.Checkbox 
$os0_cbox.Location = New-Object System.Drawing.Point(290, 68) 
$os0_cbox.AutoSize = $true
$os0_cbox.Text = "os0"
$form.Controls.Add($os0_cbox)

$vs0_cbox = New-Object System.Windows.Forms.Checkbox 
$vs0_cbox.Location = New-Object System.Drawing.Point(290, 83) 
$vs0_cbox.AutoSize = $true
$vs0_cbox.Text = "vs0"
$form.Controls.Add($vs0_cbox)

$sa0_cbox = New-Object System.Windows.Forms.Checkbox 
$sa0_cbox.Location = New-Object System.Drawing.Point(290, 98) 
$sa0_cbox.AutoSize = $true
$sa0_cbox.Text = "sa0"
$form.Controls.Add($sa0_cbox)

$pd0_cbox = New-Object System.Windows.Forms.Checkbox 
$pd0_cbox.Location = New-Object System.Drawing.Point(290, 113) 
$pd0_cbox.AutoSize = $true
$pd0_cbox.Text = "pd0"
$form.Controls.Add($pd0_cbox)

$binpkg_label = New-Object System.Windows.Forms.Label
$binpkg_label.Location = New-Object System.Drawing.Point(285, 133)
$binpkg_label.Text = '*-XX.bin + pkg'
$binpkg_label.AutoSize = $true
$binpkg_label.Font = 'Microsoft Sans Serif,6,style=Bold'
$form.Controls.Add($binpkg_label)

$cp_cbox = New-Object System.Windows.Forms.Checkbox 
$cp_cbox.Location = New-Object System.Drawing.Point(290, 145) 
$cp_cbox.AutoSize = $true
$cp_cbox.Text = "cp"
$form.Controls.Add($cp_cbox)

$s0_cbox = New-Object System.Windows.Forms.Checkbox 
$s0_cbox.Location = New-Object System.Drawing.Point(350, 10)
$s0_cbox.AutoSize = $true
$s0_cbox.Text = "syscon_fw"
$form.Controls.Add($s0_cbox)

$s2_cbox = New-Object System.Windows.Forms.Checkbox 
$s2_cbox.Location = New-Object System.Drawing.Point(350, 25)
$s2_cbox.AutoSize = $true
$s2_cbox.Text = "syscon_unk"
$form.Controls.Add($s2_cbox)

$s3_cbox = New-Object System.Windows.Forms.Checkbox 
$s3_cbox.Location = New-Object System.Drawing.Point(350, 40)
$s3_cbox.AutoSize = $true
$s3_cbox.Text = "syscon_dl"
$form.Controls.Add($s3_cbox)

$m0_cbox = New-Object System.Windows.Forms.Checkbox 
$m0_cbox.Location = New-Object System.Drawing.Point(350, 55) 
$m0_cbox.AutoSize = $true
$m0_cbox.Text = "motion0"
$form.Controls.Add($m0_cbox)

$m1_cbox = New-Object System.Windows.Forms.Checkbox 
$m1_cbox.Location = New-Object System.Drawing.Point(350, 70) 
$m1_cbox.AutoSize = $true
$m1_cbox.Text = "motion1"
$form.Controls.Add($m1_cbox)

$bic0_cbox = New-Object System.Windows.Forms.Checkbox 
$bic0_cbox.Location = New-Object System.Drawing.Point(350, 85) 
$bic0_cbox.AutoSize = $true
$bic0_cbox.Text = "bic_fw"
$form.Controls.Add($bic0_cbox)

$bic1_cbox = New-Object System.Windows.Forms.Checkbox 
$bic1_cbox.Location = New-Object System.Drawing.Point(350, 100) 
$bic1_cbox.AutoSize = $true
$bic1_cbox.Text = "bic_df"
$form.Controls.Add($bic1_cbox)

$t0_cbox = New-Object System.Windows.Forms.Checkbox 
$t0_cbox.Location = New-Object System.Drawing.Point(350, 115) 
$t0_cbox.AutoSize = $true
$t0_cbox.Text = "touch_fw"
$form.Controls.Add($t0_cbox)

$t1_cbox = New-Object System.Windows.Forms.Checkbox 
$t1_cbox.Location = New-Object System.Drawing.Point(350, 130) 
$t1_cbox.AutoSize = $true
$t1_cbox.Text = "touch_cfg"
$form.Controls.Add($t1_cbox)

$com_cbox = New-Object System.Windows.Forms.Checkbox 
$com_cbox.Location = New-Object System.Drawing.Point(350, 145) 
$com_cbox.AutoSize = $true
$com_cbox.Text = "com"
$form.Controls.Add($com_cbox)



$form.Topmost = $true

while ($true) {

    $result = $form.ShowDialog()

    if ($result -eq [System.Windows.Forms.DialogResult]::OK) {
        $cfwimg_fw = '-fw ' + $fwBox.Text
        $cfwimg_type = '-t ' + $fwType.SelectedIndex
        $cfwimg_hw = '-hw ' + $hw_rev_Box.Text + ' ' + $hw_mask_Box.Text
        $cfwimg_msg = '-msg ' + $info_Box.Text
        $cfwimg_op = '-gui'
        if ($gp_cbox.Checked) {
            $cfwimg_op = $cfwimg_op + ' -gp ' + $fwBox.Text
        }

        $cfwimg_li = '-li '
        $cfwimg_ld = '-ld '
        if ($enso_cbox.Checked) {
            $cfwimg_li = $cfwimg_li + '0'
        }
        if ($slb2_cbox.Checked) {
            $cfwimg_li = $cfwimg_li + '2'
        }
        if ($os0_cbox.Checked) {
            $cfwimg_li = $cfwimg_li + '3'
        }
        if ($vs0_cbox.Checked) {
            $cfwimg_li = $cfwimg_li + '4'
        }
        if ($sa0_cbox.Checked) {
            $cfwimg_li = $cfwimg_li + 'C'
        }
        if ($pd0_cbox.Checked) {
            $cfwimg_li = $cfwimg_li + 'E'
        }
        if ($s0_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + '0'
        }
        if ($s2_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + '2'
        }
        if ($s3_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + '3'
        }
        if ($m0_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + '4'
        }
        if ($m1_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + '5'
        }
        if ($cp_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + '6'
        }
        if ($bic0_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + '7'
        }
        if ($bic1_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + '8'
        }
        if ($t0_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + '9'
        }
        if ($t1_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + 'A'
        }
        if ($com_cbox.Checked) {
            $cfwimg_ld = $cfwimg_ld + 'B'
        }

        $cfwimg_li = $cfwimg_li + 'G'
        $cfwimg_ld = $cfwimg_ld + 'G'

        Start-Process -FilePath 'wsl.exe' -ArgumentList './mkcfw', $fnameBox.Text, $cfwimg_op, $cfwimg_fw, $cfwimg_type, $cfwimg_hw, $cfwimg_msg, $cfwimg_li, $cfwimg_ld
    }

    if ($result -eq [System.Windows.Forms.DialogResult]::Yes) {
        Start-Process -FilePath 'wsl.exe' -ArgumentList './mkcfw', $fnameBox.Text, '-gui', '-i'
    }

    if ($result -eq [System.Windows.Forms.DialogResult]::Cancel) {
        exit
    }
}