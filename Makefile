voting_system: voting_system.c
	gcc -o voting_system voting_system.c -pthread -lrt

clean:
	rm -f voting_system vote_log.txt 

clear_logs:
	rm -f vote_log_*.txt 