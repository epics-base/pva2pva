
DOT+=structs.dot

MSC+=name-search.msc
MSC+=basic.msc
MSC+=monitor-normal.msc

PNG+=$(DOT:%.dot=%.png)
PNG+=$(MSC:%.msc=%.png)

SVG+=$(DOT:%.dot=%.svg)
SVG+=$(MSC:%.msc=%.svg)

all: png svg

png: $(PNG)
svg: $(SVG)

%.png: %.dot
	dot -o $@ -Tpng $<

%.svg: %.dot
	dot -o $@ -Tsvg $<

## broken with debian 8
#%.png: %.msc
#	mscgen -T png -o $@ -i $<
%.png: %.svg
	convert $< $@

%.svg: %.msc
	mscgen -T svg -o $@ -i $<

clean:
	rm -f $(PNG)
	rm -f $(SVG)
