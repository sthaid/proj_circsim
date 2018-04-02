
TARGETS = gen_resistor_grid moving_average_test model_test1 model_test2

all: $(TARGETS)

gen_resistor_grid: gen_resistor_grid.c
	cc -o gen_resistor_grid -I.. -I../util -Wall -g -O0 gen_resistor_grid.c

moving_average_test: moving_average_test.c
	cc -o moving_average_test -I.. -I../util -Wall -g -O0 moving_average_test.c

model_test1: model_test1.c
	cc -o model_test1 -I.. -I../util -Wall -g -O0 -lreadline model_test1.c

model_test2: model_test2.c
	cc -o model_test2 -I.. -I../util -Wall -g -O0 -lreadline model_test2.c

clean:
	rm -f $(TARGETS)
