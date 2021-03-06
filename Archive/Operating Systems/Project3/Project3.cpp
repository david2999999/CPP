// David Jiang
// Project 3

#include "pch.h"
#include <stdio.h> 
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <iomanip>
#include <limits>
#include "pthread.h"
#include "semaphore.h"
#include "sched.h"
#include "setpath_defs.h"
#include "turbine_setpath_fn.h"
#include "turbine_defines.h"
#include "time_functions.h"

#define HAVE_STRUCT_TIMESPEC
#define TOTAL_GENERATOR (ROWCOUNT * COLCOUNT)

using namespace std;

struct position {
	int m;
	int n;
};

float previous_vals[ROWCOUNT][COLCOUNT];
float current_vals[ROWCOUNT][COLCOUNT];
float maxvals[ROWCOUNT][COLCOUNT];
bool generator_running[ROWCOUNT][COLCOUNT];

position positions[ROWCOUNT][COLCOUNT];
pthread_t pthreads[ROWCOUNT][COLCOUNT];

// this is used to populate current_vals and maxvals
int current_row = 0, current_col = 0, max_row = 0, max_col = 0;
float current_demand; // current input demand

int generator_count = 0;
bool generator_not_running = true;

pthread_mutex_t conditional_mutext = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t generator_completed = PTHREAD_COND_INITIALIZER;
pthread_cond_t generator_start = PTHREAD_COND_INITIALIZER;

void readMaxValuesFromFile(void *fileDestination);
void readCurrentValsFromFile(void *fileDestination);
void printCurrentValsToOutputFile(void *outfile);

float getValueFrom(int row, int col);
int getDivisor(int row, int col);
void parseString(string str, boolean toggleFile);
bool threadFinishedRunning(int r, int c);

void *generator(void *pos);

int main()
{
	string str;
	ifstream maxValues;
	ifstream currentVals;
	ofstream writeFile;

	cout << "file path --" << in_path << endl;
	cout << "out path --" << out_path << endl;

 	maxValues.open(in_path);
	currentVals.open(in_path);
	writeFile.open(out_path);

	// if the read/write file can not be opened, exit the program
	if (!maxValues) {
		cerr << "Unable to open file of max values";
		exit(1);
	}
	if (!currentVals) {
		cerr << "Unable to open file of max values";
		exit(1);
	}

	readMaxValuesFromFile(&maxValues);
	readCurrentValsFromFile(&currentVals);

	// creating all of the pthreads, each of the pthread is giving a position
	// based on the row and col
	for(int i = 0; i < ROWCOUNT; i++) {
		for (int j = 0; j < COLCOUNT; j++) {
			positions[i][j].m = i;
			positions[i][j].n = j;
			pthread_create(&pthreads[i][j], NULL, generator, &positions[i][j]);
			generator_running[i][j] = true;
		}
	}

	pthread_mutex_lock(&conditional_mutext);
	while (generator_count != TOTAL_GENERATOR) {
		pthread_cond_wait(&generator_completed, &conditional_mutext);
	}
	pthread_mutex_unlock(&conditional_mutext);

	writeFile << "Initial value of generators" << endl;
	printCurrentValsToOutputFile(&writeFile);

	getline(currentVals, str);
	while (currentVals >> current_demand) {
		swap(current_vals, previous_vals);	// assign the previous value to the current value

		// reset the generators
		for (int r = 0; r < ROWCOUNT; r++) {
			for (int c = 0; c < COLCOUNT; c++) {
				generator_running[r][c] = false;
			}
		}

		pthread_mutex_lock(&conditional_mutext);
		generator_count = 0;
		pthread_cond_broadcast(&generator_start);
		while (generator_count != TOTAL_GENERATOR) {	// waiting for all of the generators to complete 1 cycle
			pthread_cond_wait(&generator_completed, &conditional_mutext);
		}
		pthread_mutex_unlock(&conditional_mutext);

		writeFile << "Target power output for generators " << current_demand << endl;
		printCurrentValsToOutputFile(&writeFile);

		millisleep(TFARM_CYCLE_TIME * 1000.00);
	}

	return 0;
}

// generator function for all of the pthreads
void *generator(void *pos) {
	position *currentPosition = (position*)pos;
	int row = (*currentPosition).m;
	int col = (*currentPosition).n;

	while (1) {
		pthread_mutex_lock(&conditional_mutext);
		generator_count++;
		generator_running[row][col] = true;
		pthread_cond_signal(&generator_completed);

		do {
			pthread_cond_wait(&generator_start, &conditional_mutext);
		} while (threadFinishedRunning(row, col));

		pthread_mutex_unlock(&conditional_mutext);

		// calculating average and assign the appropriate value after comparison
		float prev_value = previous_vals[row][col];
		float max_val = maxvals[row][col];

		int divisor = getDivisor(row, col);
		float sum = getValueFrom(row, col) + getValueFrom(row - 1, col) + getValueFrom(row + 1, col) + getValueFrom(row, col - 1) + getValueFrom(row, col + 1);
		float avg = sum / (float)divisor;

		if (avg < current_demand) {
			prev_value += (float)(current_vals[row][col] * .3);

			if (prev_value >= max_val) current_vals[row][col] = max_val;
			else current_vals[row][col] = prev_value;

		}
		else if (avg > current_demand) {
			prev_value -= (float)(current_vals[row][col] * .3);

			if (prev_value < 0) current_vals[row][col] = 0;
			else current_vals[row][col] = prev_value;

		}
		else {
			current_vals[row][col] = prev_value;
		}
	}

	pthread_exit(NULL);
	return NULL;
}

// retrieve the value from a specific row and col
float getValueFrom(int row, int col) {
	// if index out of bound, return 0
	if (row < 0 || row > ROWCOUNT - 1 || col < 0 || col > COLCOUNT - 1) return 0;

	// return the value from the row and index from the current value
	return previous_vals[row][col];
}

// get the number of neighbors from a specific row and col
// and also including itself in the divisor
int getDivisor(int row, int col) {
	if ((row == 0 && col == 0) ||
		(row == ROWCOUNT - 1 && col == COLCOUNT - 1) ||
		(row == 0 && col == COLCOUNT - 1) ||
		(row == ROWCOUNT - 1 && col == 0)) return 3;
	else if (row == 0 || col == 0 || row == ROWCOUNT - 1 || col == COLCOUNT - 1) return 4;
	else return 5;
}

// read the max values from file and store it into the maxvals array
void readMaxValuesFromFile(void *fileDestination) {
	ifstream *readFile = (ifstream*)fileDestination;
	int lineCounter = 0;
	string line;

	while (1) {
		getline(*readFile, line);

		if (lineCounter > 0 && lineCounter <= ROWCOUNT) {
			parseString(line, false);
		}

		lineCounter++;
		if (lineCounter > ROWCOUNT) break;
	}
}

// read the current values from file and store it into the current_vals array
void readCurrentValsFromFile(void *fileDestination) {
	ifstream *readFile = (ifstream*)fileDestination;
	int lineCounter = 0;
	string line;

	while (1) {
		getline(*readFile, line);

		if (lineCounter >= (ROWCOUNT + 2) && lineCounter <= (2 * ROWCOUNT + 1)) {
			parseString(line, true);
		}

		lineCounter++;
		if (lineCounter > (2 * ROWCOUNT + 1)) break;
	}
}

// parsing each line that is read from the file
// each line will be split by spaces
void parseString(string str, boolean toggleFile) {
	string token, delimiter = " ", s = str;
	int position = 0;
	float numValue;

	while ((position = s.find(delimiter)) != string::npos) {
		token = s.substr(0, position);	// retrieve a single word from the line
		numValue = strtof((token).c_str(), 0);	// converting individual string into float number

		if (toggleFile) {
			current_vals[current_row][current_col] = numValue;
			previous_vals[current_row][current_col] = numValue;
			current_col++;

			if (current_col == COLCOUNT) {
				current_row++;
				current_col = 0;
			}
		}
		else {
			maxvals[max_row][max_col++] = numValue;
			if (max_col == COLCOUNT) {
				max_row++;
				max_col = 0;
			}
		}
		s.erase(0, position + delimiter.length());	// removing the word from the line
	}
}

// print the current values to the output file
void printCurrentValsToOutputFile(void *outfile) {
	ofstream *writeFile = (ofstream*)outfile;

	if (writeFile) {
		for (int i = 0; i < ROWCOUNT; i++) {
			for (int j = 0; j < COLCOUNT; j++) {
				*writeFile << current_vals[i][j] << " ";

				if ((j + 1) % COLCOUNT == 0) *writeFile << endl;
			}
		}
	}

	*writeFile << "**********" << endl;
}

// check if one of the generators has finished running (finished the calculation average)
bool threadFinishedRunning(int r, int c) {
	return generator_running[r][c];
}