# stdio.h
Personal implementation of stdio.h in C for Linux and Windows

Exista cate o implementare pentru fiecare dintre SO, deoarece se folosesc comenzi de sistem
specifice GNU Unix, respectiv Windows32.

Pentru citirea si scrierea in fisier, se va folosi un buffer intern pentru a micsora numarul
de schimbari de context efectuate, asadar micsorarea overhead-ului. Se va citi, respectiv scrie
in fisier doar in momentul in care nu mai exista date neprocesate in buffer.
