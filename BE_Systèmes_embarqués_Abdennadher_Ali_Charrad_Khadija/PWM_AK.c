

/*
 * ThreadPWM_AK.c
 * 
 * Copyright 2023 emblabs <emblabs@localhost.localdomain>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */

#include <gpiod.h>
#include <sqlite3.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h> 
#include <string.h>
#include <unistd.h>
#include <stdlib.h> 
#include <limits.h>
#include <signal.h> 
#include <time.h>  
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip.h>


char kbhit(void);
void init_timer(void);
void Tstimer_thread(union sigval sv);

struct sigevent sev;
struct gpiod_chip *chip;
struct gpiod_line *lineRed;
timer_t timerid;
struct sigevent sev;
struct itimerspec trigger;

pthread_cond_t cv,pwm;
pthread_mutex_t lock,lock1;
u_int32_t T=1000000, c=0,duty=0;

int db_insert();
unsigned int db_countrow();
struct meter_param* db_read();
int db_create_table();
sqlite3 *db_open();
sqlite3 *db1;
struct meter_param {//structure to hold energy meter parameters
	u_int32_t id;
	u_int32_t adc;
	time_t date;
};
time_t get_date_time(void) {
    return time(NULL); 
}
void Tstimer_thread(union sigval sv) 
{

       static int i = 0;

    // Toggle LED value
    gpiod_line_set_value(lineRed, (i & 2) != 0);
    i++;
	
  
    puts("100ms elapsed.");
		pthread_mutex_lock(&lock);
		pthread_cond_signal(&cv);
		pthread_mutex_unlock(&lock);
    timer_settime(timerid, 0, &trigger, NULL);
}

void *thread_adc(void *v) {
	int f;
	char a[5];
	while(1)
	{

	pthread_mutex_lock(&lock);
	pthread_cond_wait(&cv,&lock);
	pthread_mutex_unlock(&lock);
		f=open("/sys/devices/platform/ocp/44e0d000.tscadc/TI-am335x-adc.0.auto/iio:device0/in_voltage3_raw",O_RDONLY);
		read(f,&a,sizeof(a));
		c=atoi(a);
		printf( "the value is %d \n",c);
		close(f);
		
	pthread_mutex_lock(&lock);
	pthread_cond_signal(&pwm);
	pthread_mutex_unlock(&lock);
	}
	return NULL;
}
void *thread_pwm(void *v) {
	int fd;
	char str[10];
	while(1)
	{

	pthread_mutex_lock(&lock);
	pthread_cond_wait(&pwm,&lock);
	pthread_mutex_unlock(&lock);
	fd=open("/sys/devices/platform/ocp/48304000.epwmss/48304200.pwm/pwm/pwmchip7/pwm-7:0/duty_cycle",O_WRONLY);
	duty= (2 * T / 10 ) + (8 * T / 10 * c >> 12 );
	sprintf(str, "%d",duty);
	write(fd,str,sizeof(str));
	close(fd);	
	
	
	}
	return NULL;
}

void init_timer(void)
{
        memset(&sev, 0, sizeof(struct sigevent));
        memset(&trigger, 0, sizeof(struct itimerspec));


        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_notify_function = &Tstimer_thread;
        

        timer_create(CLOCK_REALTIME, &sev, &timerid);

        trigger.it_value.tv_sec = 0;
        trigger.it_value.tv_nsec = 100000000;  // 100ms


        timer_settime(timerid, 0, &trigger, NULL);
        
}
void init_pwm(void)
{
	char str[10];
	int fd=open("/sys/devices/platform/ocp/ocp:P8_19_pinmux/state",O_WRONLY);
	write(fd,"pwm",sizeof("pwm"));
	close(fd);
	
	fd=open("/sys/devices/platform/ocp/48304000.epwmss/48304200.pwm/pwm/pwmchip7/pwm-7:0/period",O_WRONLY);
	sprintf(str, "%d",T);
	write(fd,str,sizeof(str));
	close(fd);	
	
	fd=open("/sys/devices/platform/ocp/48304000.epwmss/48304200.pwm/pwm/pwmchip7/pwm-7:0/duty_cycle",O_WRONLY);
	sprintf(str, "%d",(T*2)/10);
	write(fd,str,sizeof(str));
	close(fd);	
	
	fd=open("/sys/devices/platform/ocp/48304000.epwmss/48304200.pwm/pwm/pwmchip7/pwm-7:0/enable",O_WRONLY);
	write(fd,"1",sizeof("1"));
	close(fd);	
}
//Creation de la base de données

sqlite3 *db_open(char *dbname)
{
	sqlite3 *db;
	char dbfile[25];
	int rc;
	
	sprintf(dbfile,"%s.db",dbname);
	
	rc = sqlite3_open(dbfile,&db);
	if(rc){
	     printf("XXXX : Can't open database: %s\n", sqlite3_errmsg(db));
	     return NULL;
	}
    	
	printf("@=> Database \"%s\" created/opened successfully\n",dbfile);
	return db;
}
// Création d'ine table dans la base de données
int db_create_table(sqlite3 *db,char *table_name)
{
	char *zErrMsg = NULL,sql[300];
	int rc;

	
   	sprintf(sql,"CREATE TABLE %s (ID INTEGER  NOT NULL PRIMARY KEY AUTOINCREMENT," \
   	"ADC INTEGER,"\
         "Time TEXT); " ,table_name);

	
	rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg);
	if(rc!=SQLITE_OK){
		printf("XXXX : SQL error (Table = %s) - %s\n",table_name,zErrMsg);
	    	sqlite3_free(zErrMsg);
		return -1;
	}

	printf("@=> Table \"%s\" created successfully!!\n",table_name);
	return 0;
}
//Fonction pour l'insertion des valeurs dans la base de données
int db_insert(sqlite3 *db,char *table_name,struct meter_param *m1)
{
	char *zErrMsg = NULL,sql[300];
	int rc;

	sprintf(sql, "INSERT INTO %s (ADC,Time) " "VALUES (%d,%ld);", table_name, m1->adc,m1->date);
	
	rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg);
	if( rc != SQLITE_OK ){
		printf("XXXX : SQL error - %s\n", zErrMsg);
	    	sqlite3_free(zErrMsg);
		return -1;
	}

	printf("@=> Database values inserted successfully!!\n");
	return 0;
}
//Fonction de lecture des valeurs dans le base 
struct meter_param* db_read(sqlite3 *db,char *table_name,unsigned int row)
{
	struct meter_param *x= (struct meter_param*)malloc(sizeof(struct meter_param));
	char sql[100],*zErrMsg = NULL;
	int rc;
	sprintf(sql,"SELECT * FROM %s",table_name);
	rc = sqlite3_exec(db, sql, NULL, &x, &zErrMsg);
	if( rc != SQLITE_OK ){
		printf("XXXX : SQL error - %s\n", zErrMsg);
	    	sqlite3_free(zErrMsg);
		free(x);
		return NULL;
	}

	printf("@=> Values read successfully!!\n");
	return x; 
}


//Fonction de comptage de nombre de ligne dans une table
unsigned int db_countrow(sqlite3 *db,char* table_name)
{
	char sql[100],*zErrMsg = NULL;
	int rc;
	sprintf(sql,"SELECT count(*) FROM %s",table_name);
	rc = sqlite3_exec(db, sql, NULL,NULL, &zErrMsg);
	if( rc != SQLITE_OK ){
		printf("XXXX : SQL error - %s\n", zErrMsg);
	    	sqlite3_free(zErrMsg);
		return 0; 
	}

	
	return 0;

}


int main (int argc, char** argv)
{

	pthread_t *th_adc, *th_pwm;
	th_adc = (pthread_t *) malloc(sizeof(pthread_t));
	th_pwm = (pthread_t *) malloc(sizeof(pthread_t));
	const char *chipname = "gpiochip2";
    //struct itimerval timer;

    // Open GPIO chip
    chip = gpiod_chip_open_by_name(chipname);

    // Open GPIO lines
    lineRed = gpiod_chip_get_line(chip, 3);

    // Open LED lines for output
    gpiod_line_request_output(lineRed, "example1", 0);
	sqlite3 *db_open();
	db_create_table(db1, "ADC");
    init_timer();
     init_pwm();
           db1 = db_open("ADC"); 

    if (db1 == NULL) {
        printf("Erreur lors de l'ouverture de la base de données.\n");
        return EXIT_FAILURE;
    }

    
    if (db_create_table(db1, "ADC") != 0) {
        printf("Erreur lors de la création de la table.\n");
        sqlite3_close(db1);  
        return EXIT_FAILURE;
    }
     u_int32_t row_to_read = 1; 
    struct meter_param *data = db_read(db1, "ADC", row_to_read); 
    if (data) {
        printf("ID : %u\n", data->id);
        printf("ADC : %u\n", data->adc);
        printf("date : %u\n", data->adc);
        
       
        free(data);
    }
    pthread_create(th_adc, NULL, thread_adc, NULL);	
    pthread_create(th_pwm, NULL, thread_pwm, NULL);	
    // Wait for user to press 'q'
    while (kbhit() != 'q');

    // Delete (destroy) the timer
    timer_delete(timerid);
        
    // Release lines and chip
    gpiod_line_release(lineRed);
    gpiod_chip_close(chip);
	pthread_cancel(*th_adc);
	free(th_adc);
	pthread_cancel(*th_pwm);
	free(th_pwm);



}

