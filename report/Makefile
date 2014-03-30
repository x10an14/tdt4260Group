
report.pdf: report.tex DCPT-plot.tex tagged-sequential-plot.tex
	pdflatex report.tex
	bibtex report.aux
	pdflatex report.tex
	pdflatex report.tex #Two runs for references

all: report.pdf

clean:
	rm -f *.aux *.log *.pdf *.loc *.lot *.toc *.lof *-plot.eps *-plot.tex

DCPT-plot.tex : DCPT.gpi DCPT_DELTA_BITS.txt DCPT_MAX_DEGREE.txt DCPT_NUM_DELTAS.txt DCPT_TABLE_SIZE.txt
	gnuplot DCPT.gpi

tagged-sequential-plot.tex : tagged-sequential.gpi tagged-sequential-degree-1-data.txt tagged-sequential-degree-5-data.txt
	gnuplot tagged-sequential.gpi
