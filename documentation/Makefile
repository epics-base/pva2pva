
DOT+=structs.dot

MSC+=name-search.msc
MSC+=basic.msc

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

%.png: %.msc
	mscgen -T png -o $@ -i $<

%.svg: %.msc
	mscgen -T svg -o $@ -i $<

clean:
	rm -f $(PNG)
	rm -f $(SVG)
