Add-Type -AssemblyName System.Drawing

$W = 1900
$H = 1350
$bmp = New-Object System.Drawing.Bitmap($W, $H)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.Clear([System.Drawing.Color]::White)

$font = New-Object System.Drawing.Font("Microsoft YaHei", 16)
$fontSmall = New-Object System.Drawing.Font("Microsoft YaHei", 13)
$fontTitle = New-Object System.Drawing.Font("Microsoft YaHei", 25, [System.Drawing.FontStyle]::Bold)
$fontSub = New-Object System.Drawing.Font("Microsoft YaHei", 18, [System.Drawing.FontStyle]::Bold)
$fontDim = New-Object System.Drawing.Font("Arial", 14)
$pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(35, 45, 55), 2)
$penHeavy = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(20, 28, 36), 4)
$penDim = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(25, 95, 155), 1.8)
$penDash = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(105, 115, 125), 1.4)
$penDash.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
$penCenter = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(140, 80, 80), 1)
$penCenter.DashStyle = [System.Drawing.Drawing2D.DashStyle]::DashDot
$brushText = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(25, 35, 45))
$brushShell = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(232, 237, 242))
$brushScreen = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(50, 72, 92))
$brushBattery = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(238, 193, 72))
$brushSpeaker = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(105, 120, 135))
$brushPcb = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(205, 235, 214))
$brushPort = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(220, 95, 70))
$brushMic = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(58, 140, 92))

function Draw-CenteredText($text, $x, $y, $fontUse, $brushUse) {
    $sz = $g.MeasureString($text, $fontUse)
    $g.DrawString($text, $fontUse, $brushUse, $x-$sz.Width/2, $y-$sz.Height/2)
}

function Draw-ArrowHead([double]$x, [double]$y, [double]$dx, [double]$dy, $brushUse) {
    $len = [Math]::Sqrt($dx*$dx+$dy*$dy)
    if ($len -eq 0) { return }
    $ux=$dx/$len; $uy=$dy/$len
    $perpX=-$uy; $perpY=$ux
    $p1=[System.Drawing.PointF]::new([float]$x,[float]$y)
    $p2=[System.Drawing.PointF]::new(
        [float]($x-10*$ux+4*$perpX),
        [float]($y-10*$uy+4*$perpY)
    )
    $p3=[System.Drawing.PointF]::new(
        [float]($x-10*$ux-4*$perpX),
        [float]($y-10*$uy-4*$perpY)
    )
    $g.FillPolygon($brushUse,[System.Drawing.PointF[]]@($p1,$p2,$p3))
}

function Dim-H($x1,$x2,$yObj,$yDim,$label) {
    $g.DrawLine($penDim,$x1,$yObj,$x1,$yDim)
    $g.DrawLine($penDim,$x2,$yObj,$x2,$yDim)
    $g.DrawLine($penDim,$x1,$yDim,$x2,$yDim)
    Draw-ArrowHead $x1 $yDim 1 0 $brushText
    Draw-ArrowHead $x2 $yDim -1 0 $brushText
    Draw-CenteredText $label (($x1+$x2)/2) ($yDim-13) $fontDim $brushText
}

function Dim-V($xObj,$xDim,$y1,$y2,$label) {
    $g.DrawLine($penDim,$xObj,$y1,$xDim,$y1)
    $g.DrawLine($penDim,$xObj,$y2,$xDim,$y2)
    $g.DrawLine($penDim,$xDim,$y1,$xDim,$y2)
    Draw-ArrowHead $xDim $y1 0 1 $brushText
    Draw-ArrowHead $xDim $y2 0 -1 $brushText
    $state=$g.Save()
    $g.TranslateTransform($xDim-17,($y1+$y2)/2)
    $g.RotateTransform(-90)
    Draw-CenteredText $label 0 0 $fontDim $brushText
    $g.Restore($state)
}

function Rounded-RectPath($x,$y,$w,$h,$r) {
    $path=New-Object System.Drawing.Drawing2D.GraphicsPath
    $d=2*$r
    $path.AddArc($x,$y,$d,$d,180,90)
    $path.AddArc($x+$w-$d,$y,$d,$d,270,90)
    $path.AddArc($x+$w-$d,$y+$h-$d,$d,$d,0,90)
    $path.AddArc($x,$y+$h-$d,$d,$d,90,90)
    $path.CloseFigure()
    return $path
}

$g.DrawString("小智设备外壳尺寸图", $fontTitle, $brushText, 55, 30)
$g.DrawString("单位：mm  |  外形：96 × 65 × 41  |  左侧加宽6，外壳高度和厚度不变", $font, $brushText, 55, 75)
$g.DrawLine($pen,55,112,1845,112)

# FRONT VIEW
$g.DrawString("A. 前壳正视图", $fontSub, $brushText, 75, 130)
$s=8.2
$ox=115; $oy=220
$ow=96*$s; $oh=65*$s
$cx=$ox+$ow/2; $cy=$oy+$oh/2
$path=Rounded-RectPath $ox $oy $ow $oh (5*$s)
$g.FillPath($brushShell,$path); $g.DrawPath($penHeavy,$path)
$g.DrawLine($penCenter,$cx,$oy-10,$cx,$oy+$oh+10)
$g.DrawLine($penCenter,$ox-10,$cy,$ox+$ow+10,$cy)

# PCB reference
$pcbX=$cx+(3-41)*$s; $pcbY=$cy-(23+3)*$s
$g.DrawRectangle($penDash,$pcbX,$pcbY,82*$s,46*$s)
$g.DrawString("PCB 82 × 46（参考）",$fontSmall,$brushText,$pcbX+8,$pcbY+8)

# Screen aperture 37.5 x 59.2, shell centre (3.516,0)
$scx=$cx+3.516*$s; $scy=$cy
$sw=37.5*$s; $sh=59.2*$s
$g.FillRectangle($brushScreen,$scx-$sw/2,$scy-$sh/2,$sw,$sh)
$g.DrawRectangle($penHeavy,$scx-$sw/2,$scy-$sh/2,$sw,$sh)
$g.DrawString("屏幕开孔",$fontSmall,[System.Drawing.Brushes]::White,$scx-$sw/2+10,$scy-8)

# PCB mounting-hole references
$holes=@(@(-37.425,17.263),@(-37.425,-20.938),@(37.613,-0.815))
foreach($h in $holes){
    $hx=$cx+($h[0]+3)*$s; $hy=$cy-($h[1]+3)*$s
    $g.DrawEllipse($penDash,$hx-1.5*$s,$hy-1.5*$s,3*$s,3*$s)
    $g.DrawLine($penCenter,$hx-8,$hy,$hx+8,$hy)
    $g.DrawLine($penCenter,$hx,$hy-8,$hx,$hy+8)
}

# Microphone array
$mx=$cx+(29.899+3)*$s; $my=$cy-(12.504+3)*$s
$vent=2.2*$s/2
foreach($a in @(0,60,120,180,240,300)){
    $rad=$a*[Math]::PI/180
    $px=$mx+4.1*$s*[Math]::Cos($rad); $py=$my-4.1*$s*[Math]::Sin($rad)
    $g.FillEllipse($brushMic,$px-$vent,$py-$vent,2*$vent,2*$vent)
}
$g.FillEllipse($brushMic,$mx-$vent,$my-$vent,2*$vent,2*$vent)
$g.DrawEllipse($penDash,$mx-7*$s,$my-7*$s,14*$s,14*$s)

Dim-H $ox ($ox+$ow) ($oy+$oh) ($oy+$oh+58) "96"
Dim-V $ox ($ox-58) $oy ($oy+$oh) "65"
Dim-H ($scx-$sw/2) ($scx+$sw/2) ($scy-$sh/2) ($oy-35) "37.50"
Dim-V ($scx+$sw/2) ($scx+$sw/2+42) ($scy-$sh/2) ($scy+$sh/2) "59.20"

# REAR LAYOUT
$g.DrawString("B. 后壳内部布局", $fontSub, $brushText, 1030, 130)
$s2=6.4
$rx=1110; $ry=220; $rw=96*$s2; $rh=65*$s2
$rcx=$rx+$rw/2; $rcy=$ry+$rh/2
$rpath=Rounded-RectPath $rx $ry $rw $rh (5*$s2)
$g.FillPath($brushPcb,$rpath); $g.DrawPath($penHeavy,$rpath)

# PCB envelope dashed
$g.DrawRectangle($penDash,$rcx+(3-41)*$s2,$rcy-(23+3)*$s2,82*$s2,46*$s2)

# Battery installed 68 x 35 at (3.1,2)
$bx=$rcx+3.1*$s2; $by=$rcy-2*$s2
$bw=68*$s2; $bh=35*$s2
$g.FillRectangle($brushBattery,$bx-$bw/2,$by-$bh/2,$bw,$bh)
$g.DrawRectangle($pen,$bx-$bw/2,$by-$bh/2,$bw,$bh)
Draw-CenteredText "电池 68 × 35 × 10" $bx $by $font $brushText

# Upright speaker footprint 7 x 35 at the left wall, centre (-42.1,1.1625)
$spx=$rcx-42.1*$s2; $spy=$rcy-1.1625*$s2
$spw=7*$s2; $sph=35*$s2
$g.FillRectangle($brushSpeaker,$spx-$spw/2,$spy-$sph/2,$spw,$sph)
$g.DrawRectangle($pen,$spx-$spw/2,$spy-$sph/2,$spw,$sph)
Draw-CenteredText "喇叭`n7×35`n立高25" $spx $spy $fontSmall ([System.Drawing.Brushes]::White)

# PCB bosses
foreach($h in $holes){
    $hx=$rcx+($h[0]+3)*$s2; $hy=$rcy-($h[1]+3)*$s2
    $g.FillEllipse([System.Drawing.Brushes]::White,$hx-2.7*$s2,$hy-2.7*$s2,5.4*$s2,5.4*$s2)
    $g.DrawEllipse($pen,$hx-2.7*$s2,$hy-2.7*$s2,5.4*$s2,5.4*$s2)
    $g.DrawEllipse($pen,$hx-1.35*$s2,$hy-1.35*$s2,2.7*$s2,2.7*$s2)
}

# Switch zone
$sx=$rcx+42*$s2; $sy=$rcy-22*$s2
$g.FillRectangle($brushPort,$sx-5*$s2,$sy-6*$s2,10*$s2,12*$s2)
$g.DrawString("开关",$fontSmall,$brushText,$sx-18,$sy+40)

Dim-H ($bx-$bw/2) ($bx+$bw/2) ($by-$bh/2) ($ry-30) "68"
Dim-V ($bx-$bw/2) ($rx-40) ($by-$bh/2) ($by+$bh/2) "35"
Dim-H ($spx-$spw/2) ($spx+$spw/2) ($spy+$sph/2) ($ry+$rh+42) "7"
Dim-V ($spx+$spw/2) ($spx+$spw/2+42) ($spy-$sph/2) ($spy+$sph/2) "35"

# SIDE VIEW / TYPE-C
$g.DrawString("C. 侧视图及Type-C孔", $fontSub, $brushText, 1030, 810)
$sx0=1105; $sy0=900; $ss=7.4
$sideW=96*$ss; $sideH=41*$ss
$g.FillRectangle($brushShell,$sx0,$sy0,$sideW,$sideH)
$g.DrawRectangle($penHeavy,$sx0,$sy0,$sideW,$sideH)
$g.DrawLine($penDash,$sx0,$sy0+21*$ss,$sx0+$sideW,$sy0+21*$ss)
$g.DrawString("前壳 21",$fontSmall,$brushText,$sx0+12,$sy0+12)
$g.DrawString("后壳 20",$fontSmall,$brushText,$sx0+12,$sy0+21*$ss+8)

$portW=13.4*$ss; $portH=7.4*$ss
$portX=$sx0+(48+3.516)*$ss-$portW/2
$portY=$sy0+4*$ss
$g.FillRectangle($brushPort,$portX,$portY,$portW,$portH)
$g.DrawRectangle($penHeavy,$portX,$portY,$portW,$portH)
Draw-CenteredText "Type-C" ($portX+$portW/2) ($portY+$portH/2) $fontSmall ([System.Drawing.Brushes]::White)

Dim-H $portX ($portX+$portW) $portY ($sy0-35) "13.40"
Dim-V ($portX+$portW) ($portX+$portW+45) $portY ($portY+$portH) "7.40"
Dim-V $portX ($portX-45) $sy0 $portY "4.00"
Dim-V ($sx0+$sideW) ($sx0+$sideW+50) $sy0 ($sy0+$sideH) "41"

$g.DrawString("关键间隙：喇叭-PCB边缘 0.6  |  喇叭-电池 7.7  |  支撑片宽1.625（≥1.2） |  Type-C上边距 4.0", $fontSmall, $brushText, 70, 1290)

$out = Join-Path (Get-Location) "enclosure\xiaozhi_dimension_drawing.png"
$bmp.Save($out,[System.Drawing.Imaging.ImageFormat]::Png)

$g.Dispose(); $bmp.Dispose()
$font.Dispose(); $fontSmall.Dispose(); $fontTitle.Dispose(); $fontSub.Dispose(); $fontDim.Dispose()
$pen.Dispose(); $penHeavy.Dispose(); $penDim.Dispose(); $penDash.Dispose(); $penCenter.Dispose()
$brushText.Dispose(); $brushShell.Dispose(); $brushScreen.Dispose(); $brushBattery.Dispose()
$brushSpeaker.Dispose(); $brushPcb.Dispose(); $brushPort.Dispose(); $brushMic.Dispose()
Write-Output $out

