
report.pdf: report.tex DCPT-delta-bits-plot.tex DCPT-table-size-plot.tex DCPT-num-deltas-plot.tex tagged-sequential-plot.tex benchmarks-plot.tex bibliography.bib abstract.tex background.tex config.tex conclusion.tex discussion.tex introduction.tex methodology.tex results.tex the_scheme.tex
	pdflatex --shell-escape report.tex
	bibtex report.aux
	pdflatex --shell-escape report.tex
	pdflatex --shell-escape report.tex #Two runs for references

slides.pdf: slides.tex DCPT-delta-bits-plot.tex DCPT-table-size-plot.tex DCPT-num-deltas-plot.tex tagged-sequential-plot.tex benchmarks-plot.tex
	pdflatex --shell-escape slides.tex

all: report.pdf slides.pdf

clean:
	rm -f *.aux *.log *.pdf *.loc *.lot *.toc *.lof *-plot.eps *-plot.tex *.nav *.out *.snm *.toc

DCPT-delta-bits-plot.tex : DCPT_DELTA_BITS.gpi DCPT_DELTA_BITS.txt
	gnuplot DCPT_DELTA_BITS.gpi

DCPT-table-size-plot.tex : DCPT_TABLE_SIZE.gpi DCPT_TABLE_SIZE.txt
	gnuplot DCPT_TABLE_SIZE.gpi

DCPT-num-deltas-plot.tex : DCPT_NUM_DELTAS.gpi DCPT_NUM_DELTAS.txt
	gnuplot DCPT_NUM_DELTAS.gpi

tagged-sequential-plot.tex : tagged-sequential.gpi tagged-sequential-degree-1-data.txt tagged-sequential-degree-5-data.txt
	gnuplot tagged-sequential.gpi

benchmarks-plot.tex : benchmarks.gpi benchmarks-data.txt
	gnuplot benchmarks.gpi
