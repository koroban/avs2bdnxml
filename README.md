AVS to BluRay SUP/PGS and BDN XML
=================================

This program can be used to transform AviSynth scripts, which produce RGBA
output, to BDN XML+PNG format. This in turn can be transformed into a SUP file,
which can be used to master a BluRay disc with subtitles.

Usage instructions
------------------

0. If you want to build it:
On Ubuntu:

```
sudo apt-get install mingw32 mingw32-binutils
make
```

1. Prepare subtitles. You can either produce subtitles in a normal format like
SRT or ASS/SSA, or produce an RGBA video beforehand.

2. Create an AviSynth script. If you made a regular subtitle file, you can use
something like this:

```
video=AviSource("video.avi")
# This requires at least VSFilter 2.39
MaskSub("subtitles.ext",video.width,video.height, video.framerate,video.framecount)
```

If you created an RGBA video, do something like this instead:

```
AviSource("subtitles_RGBA.avi")
FlipVertical()
```

3. Run the program:

```
avs2bdnxml -t Undefined -l und -v 1080p -f 23.976 -a1 -p1 -b0 -m3 -u0 -e0 -n0 -z0 -o output.xml input.avs
```

4. You get a BDN XML file in the following format:

```
    <?xml version="1.0" encoding="UTF-8"?>
    <BDN Version="0.93" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
    xsi:noNamespaceSchemaLocation="BD-03-006-0093b BDN File Format.xsd">
    <Description>
    <Name Title="Undefined" Content=""/>
    <Language Code="und"/>
    <Format VideoFormat="[ 480i / 480p / 576i / 720p / 1080i /1080p ]" FrameRate="[ 23.976 / 24 / 25 / 29.97 / 50 / 59.94 ]" DropFrame="false"/>
    <Events LastEventOutTC="00:00:00:00" FirstEventInTC="00:00:00:00" ContentInTC="00:00:00:00"
    ContentOutTC="00:00:00:00" NumberofEvents="[ number of encoded frames ]" Type="Graphic"/>
    </Description>
    <Events>
    <Event Forced="[ False / True ]" InTC="00:00:00:00" OutTC="00:00:00:00">
    <Graphic Width="0" Height="0" X="0" Y="0">000000.png</Graphic>
    </Event>
    </Events>
    </BDN>
```

5. To convert output directly to BD .sup format, simply change extension for output file in programm call to .sup.

**Don't use BDSupEdit or BDSup2Sub if you enabled -b for splitting images in multiple parts!**


Detail informations on [doom9](http://forum.doom9.org/showthread.php?t=146493)

> http://ps-auxw.de/avs2bdnxml/
