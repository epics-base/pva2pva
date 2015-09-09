
all: structs.png

%.png: %.dot
	dot -o $@ -Tpng $<

clean:
	rm -f structs.png
