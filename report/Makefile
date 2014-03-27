
report.pdf: report.tex
	pdflatex report.tex
	bibtex report.aux
	pdflatex report.tex
	pdflatex report.tex #Two runs for references

all: report.pdf

clean:
	rm -f *.aux *.log *.pdf *.loc *.lot *.toc *.lof
