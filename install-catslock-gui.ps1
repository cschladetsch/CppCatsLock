[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([System.Threading.Thread]::CurrentThread.ApartmentState -ne 'STA') {
    $scriptPath = $PSCommandPath
    $quotedPath = '"' + $scriptPath.Replace('"', '\"') + '"'
    Start-Process -FilePath powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -STA -File $quotedPath"
    exit
}

Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

$script:TaskName = 'Catslock'
$script:InstallScript = Join-Path $PSScriptRoot 'install-catslock-autostart.ps1'
$script:UninstallScript = Join-Path $PSScriptRoot 'uninstall-catslock-autostart.ps1'
$script:DefaultInstallDir = Join-Path $env:ProgramFiles 'Catslock'

function Test-CurrentUserIsAdmin {
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [System.Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Find-DefaultCatslockExe {
    $candidates = @(
        (Join-Path $PSScriptRoot 'catslock.exe'),
        (Join-Path $PSScriptRoot 'build-clang-uac\catslock.exe'),
        (Join-Path $PSScriptRoot 'build-clang\catslock.exe'),
        (Join-Path $PSScriptRoot 'build\Release\catslock.exe'),
        (Join-Path $PSScriptRoot 'build\catslock.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).ProviderPath
        }
    }

    return ''
}

function Get-CatslockTaskStatus {
    $task = Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue
    if (-not $task) {
        return 'Not installed'
    }

    $info = Get-ScheduledTaskInfo -TaskName $script:TaskName -ErrorAction SilentlyContinue
    if ($info) {
        return "Installed - $($task.State); last result $($info.LastTaskResult)"
    }

    return "Installed - $($task.State)"
}

function Start-CatslockAsAdmin {
    param([string]$ExePath)

    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
        throw 'Select a valid catslock.exe first.'
    }

    Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path -Parent $ExePath) -Verb RunAs
}

function Restart-InstallerAsAdmin {
    $scriptPath = $PSCommandPath
    Start-Process -FilePath powershell.exe -Verb RunAs -ArgumentList @(
        '-NoProfile',
        '-ExecutionPolicy',
        'Bypass',
        '-STA',
        '-File',
        "`"$scriptPath`""
    )
    $script:Window.Close()
}

[xml]$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Catslock Installer"
        Width="620"
        Height="486"
        WindowStartupLocation="CenterScreen"
        ResizeMode="NoResize"
        Background="#f6f8fb"
        FontFamily="Segoe UI"
        FontSize="13">
    <Grid Margin="24">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="*"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <StackPanel Grid.Row="0" Margin="0,0,0,22">
            <TextBlock Text="Catslock Startup Installer"
                       FontSize="24"
                       FontWeight="SemiBold"
                       Foreground="#111827"/>
            <TextBlock Text="Install Catslock to start automatically in your interactive Windows session."
                       Margin="0,8,0,0"
                       Foreground="#4b5563"/>
        </StackPanel>

        <Border Grid.Row="1"
                Background="White"
                BorderBrush="#d7dde8"
                BorderThickness="1"
                CornerRadius="8"
                Padding="16"
                Margin="0,0,0,16">
            <Grid>
                <Grid.RowDefinitions>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                </Grid.RowDefinitions>
                <Grid.ColumnDefinitions>
                    <ColumnDefinition Width="*"/>
                    <ColumnDefinition Width="96"/>
                </Grid.ColumnDefinitions>

                <TextBlock Grid.Row="0"
                           Grid.ColumnSpan="2"
                           Text="Executable"
                           FontWeight="SemiBold"
                           Foreground="#111827"/>
                <TextBox x:Name="ExePathTextBox"
                         Grid.Row="1"
                         Grid.Column="0"
                         Height="34"
                         Margin="0,8,8,0"
                         VerticalContentAlignment="Center"/>
                <Button x:Name="BrowseButton"
                        Grid.Row="1"
                        Grid.Column="1"
                        Height="34"
                        Margin="0,8,0,0"
                        Content="Browse"/>
                <TextBlock Grid.Row="2"
                           Grid.ColumnSpan="2"
                           Margin="0,14,0,0"
                           Text="Install location"
                           FontWeight="SemiBold"
                           Foreground="#111827"/>
                <TextBox x:Name="InstallDirTextBox"
                         Grid.Row="3"
                         Grid.ColumnSpan="2"
                         Height="34"
                         Margin="0,8,0,0"
                         VerticalContentAlignment="Center"/>
                <CheckBox x:Name="StartNowCheckBox"
                          Grid.Row="4"
                          Grid.ColumnSpan="2"
                          Margin="0,14,0,0"
                          Content="Start Catslock after installing"
                          IsChecked="True"/>
                <TextBlock x:Name="AdminTextBlock"
                           Grid.Row="5"
                           Grid.ColumnSpan="2"
                           Margin="0,14,0,0"
                           TextWrapping="Wrap"
                           Foreground="#6b7280"/>
            </Grid>
        </Border>

        <Border Grid.Row="2"
                Background="#eef4ff"
                BorderBrush="#c7d7fe"
                BorderThickness="1"
                CornerRadius="8"
                Padding="14"
                Margin="0,0,0,16">
            <TextBlock TextWrapping="Wrap"
                       Foreground="#1f3a8a"
                       Text="Catslock uses a desktop keyboard hook, so this installer creates a Scheduled Task at logon instead of a Session 0 Windows Service."/>
        </Border>

        <Border Grid.Row="3"
                Background="White"
                BorderBrush="#d7dde8"
                BorderThickness="1"
                CornerRadius="8"
                Padding="16">
            <StackPanel>
                <TextBlock Text="Status"
                           FontWeight="SemiBold"
                           Foreground="#111827"/>
                <TextBlock x:Name="StatusTextBlock"
                           Margin="0,8,0,0"
                           TextWrapping="Wrap"
                           Foreground="#374151"/>
                <TextBox x:Name="OutputTextBox"
                         Margin="0,14,0,0"
                         Height="82"
                         IsReadOnly="True"
                         TextWrapping="Wrap"
                         VerticalScrollBarVisibility="Auto"
                         Background="#f9fafb"
                         BorderBrush="#d7dde8"/>
            </StackPanel>
        </Border>

        <DockPanel Grid.Row="4" Margin="0,18,0,0" LastChildFill="False">
            <Button x:Name="ElevateButton"
                    DockPanel.Dock="Left"
                    Width="142"
                    Height="36"
                    Content="Restart as admin"/>
            <StackPanel DockPanel.Dock="Right" Orientation="Horizontal">
                <Button x:Name="StartButton"
                        Width="92"
                        Height="36"
                        Margin="0,0,8,0"
                        Content="Start"/>
                <Button x:Name="RemoveButton"
                        Width="92"
                        Height="36"
                        Margin="0,0,8,0"
                        Content="Remove"/>
                <Button x:Name="InstallButton"
                        Width="112"
                        Height="36"
                        Content="Install"/>
            </StackPanel>
        </DockPanel>
    </Grid>
</Window>
'@

$reader = [System.Xml.XmlNodeReader]::new($xaml)
$script:Window = [Windows.Markup.XamlReader]::Load($reader)

$exePathTextBox = $script:Window.FindName('ExePathTextBox')
$installDirTextBox = $script:Window.FindName('InstallDirTextBox')
$browseButton = $script:Window.FindName('BrowseButton')
$startNowCheckBox = $script:Window.FindName('StartNowCheckBox')
$adminTextBlock = $script:Window.FindName('AdminTextBlock')
$statusTextBlock = $script:Window.FindName('StatusTextBlock')
$outputTextBox = $script:Window.FindName('OutputTextBox')
$elevateButton = $script:Window.FindName('ElevateButton')
$startButton = $script:Window.FindName('StartButton')
$removeButton = $script:Window.FindName('RemoveButton')
$installButton = $script:Window.FindName('InstallButton')

function Set-InstallerOutput {
    param([string]$Message)

    $outputTextBox.Text = $Message
    $statusTextBlock.Text = Get-CatslockTaskStatus
}

function Update-AdminState {
    if (Test-CurrentUserIsAdmin) {
        $adminTextBlock.Text = 'Running as administrator. The startup task can be installed with highest privileges.'
        $elevateButton.IsEnabled = $false
    } else {
        $adminTextBlock.Text = 'Not running as administrator. Restart as admin before installing the startup task.'
        $elevateButton.IsEnabled = $true
    }
}

$exePathTextBox.Text = Find-DefaultCatslockExe
$installDirTextBox.Text = $script:DefaultInstallDir
$statusTextBlock.Text = Get-CatslockTaskStatus
Update-AdminState

$browseButton.Add_Click({
    $dialog = [Microsoft.Win32.OpenFileDialog]::new()
    $dialog.Title = 'Select catslock.exe'
    $dialog.Filter = 'Catslock executable (catslock.exe)|catslock.exe|Executable files (*.exe)|*.exe|All files (*.*)|*.*'
    if ($exePathTextBox.Text) {
        $dialog.InitialDirectory = Split-Path -Parent $exePathTextBox.Text
    } else {
        $dialog.InitialDirectory = $PSScriptRoot
    }

    if ($dialog.ShowDialog($script:Window)) {
        $exePathTextBox.Text = $dialog.FileName
    }
})

$elevateButton.Add_Click({
    Restart-InstallerAsAdmin
})

$installButton.Add_Click({
    try {
        if (-not (Test-Path -LiteralPath $script:InstallScript -PathType Leaf)) {
            throw "Missing installer script: $script:InstallScript"
        }

        if (-not (Test-CurrentUserIsAdmin)) {
            throw 'Restart as administrator before installing.'
        }

        $arguments = @{
            TaskName = $script:TaskName
            ExePath = $exePathTextBox.Text
            InstallDir = $installDirTextBox.Text
        }
        if (-not $startNowCheckBox.IsChecked) {
            $arguments.NoStart = $true
        }

        $result = & $script:InstallScript @arguments | Out-String
        Set-InstallerOutput $result.Trim()
    } catch {
        Set-InstallerOutput $_.Exception.Message
    }
})

$removeButton.Add_Click({
    try {
        if (-not (Test-Path -LiteralPath $script:UninstallScript -PathType Leaf)) {
            throw "Missing uninstaller script: $script:UninstallScript"
        }

        if (-not (Test-CurrentUserIsAdmin)) {
            throw 'Restart as administrator before removing the startup task.'
        }

        $result = & $script:UninstallScript -TaskName $script:TaskName | Out-String
        Set-InstallerOutput $result.Trim()
    } catch {
        Set-InstallerOutput $_.Exception.Message
    }
})

$startButton.Add_Click({
    try {
        Start-CatslockAsAdmin -ExePath $exePathTextBox.Text
        Set-InstallerOutput 'Started catslock.exe.'
    } catch {
        Set-InstallerOutput $_.Exception.Message
    }
})

[void]$script:Window.ShowDialog()
