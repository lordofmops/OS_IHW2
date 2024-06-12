#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>

// Глобальные переменные для идентификаторов семафора и разделяемой памяти
sem_t sem_mutex; 
int shm_fd;
size_t shm_size;

// Структура для хранения результатов игры
typedef struct {
    int wins;
    int draws;
    int losses;
    int total_points;
} Game_Result;

// Обработчик сигнала SIGINT (Ctrl+C)
void sigint_handler(int signum) {
    // Освобождаем разделяемую память и удаляем семафоры
    sem_destroy(&sem_mutex);
    shm_unlink("/shared_memory");
    exit(0);
}

// Функция для игры в "Камень, ножницы, бумага"
int play_game() {
    sleep(1);

    srand(time(NULL));

    int student_choice = rand() % 3;   // 0 - камень, 1 - ножницы, 2 - бумага
    int opponent_choice = rand() % 3;

    if (student_choice == opponent_choice) {
        return 0;  // Ничья
    } else if ((student_choice == 0 && opponent_choice == 1) ||  // Камень против ножниц
               (student_choice == 1 && opponent_choice == 2) ||  // Ножницы против бумаги
               (student_choice == 2 && opponent_choice == 0)) {  // Бумага против камня
        return 1; // Победа студента
    } else {
        return -1; // Победа оппонента
    }
}

// Главная функция
int main(int argc, char *argv[]) {
    // Устанавливаем обработчик сигнала SIGINT
    signal(SIGINT, sigint_handler);

    if (argc != 2) {
        fprintf(stderr, "Использование: %s <количество_студентов>\n", argv[0]);
        return 1;
    }

    // Проверяем, является ли аргумент допустимым целым числом
    char *endptr;
    long num_students = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || num_students <= 0 || num_students > INT_MAX) {
        fprintf(stderr, "Недопустимое количество студентов: %s\n", argv[1]);
        return 1;
    }

    // Структура для хранения результатов игры
    typedef struct {
        Game_Result results[num_students];
    } Shared_Data;

    // Определяем размер разделяемой памяти
    shm_size = sizeof(Shared_Data);

    // Создаем и инициализируем разделяемую память
    shm_fd = shm_open("/shared_memory", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }
    if (ftruncate(shm_fd, shm_size) == -1) {
        perror("ftruncate");
        exit(1);
    }
    Shared_Data *shared_data = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Инициализируем неименованный семафор
    if (sem_init(&sem_mutex, 1, 1) == -1) {
        perror("sem_init");
        exit(1);
    }

    // Создаем дочерние процессы
    pid_t pid;
    for (int i = 0; i < num_students; i++) {
        sleep(1);
        pid = fork();
        if (pid == 0) { // Дочерний процесс
            int student_index = i;
            for (int j = i + 1; j < num_students; j++) {
                int opponent_index = j;

                // Захватываем семафор
                if (sem_wait(&sem_mutex) == -1) {
                    perror("sem_wait");
                    exit(1);
                }

                // Играем в "Камень, ножницы, бумага"
                int result = play_game();

                // Обновляем результаты игры
                if (result == 0) { // Ничья
                    printf("Студент %d и студент %d сыграли в ничью.\n", student_index, opponent_index);
                    shared_data->results[student_index].total_points++;
                    shared_data->results[opponent_index].total_points++;
                    shared_data->results[student_index].draws++;
                    shared_data->results[opponent_index].draws++;
                } else if (result == 1) { // Победа студента
                    printf("Студент %d победил студента %d.\n", student_index, opponent_index);
                    shared_data->results[student_index].total_points += 2;
                    shared_data->results[student_index].wins++;
                    shared_data->results[opponent_index].losses++;
                } else { // Победа оппонента
                    printf("Студент %d проиграл студенту %d.\n", student_index, opponent_index);
                    shared_data->results[opponent_index].total_points += 2;
                    shared_data->results[student_index].losses++;
                    shared_data->results[opponent_index].wins++;
                }

                // Освобождаем семафор
                if (sem_post(&sem_mutex) == -1) {
                    perror("sem_post");
                    exit(1);
                }

                // Переход к другому игроку
                sleep(3);
            }
            exit(0);
        } else if (pid < 0) {
            perror("fork");
            exit(1);
        }
    }

    // Ожидаем завершения всех дочерних процессов
    for (int i = 0; i < num_students; i++) {
        wait(NULL);
    }

    printf("\n Результаты:\n");
    int max_points = INT_MIN;
    for (int i = 0; i < num_students; i++) {
        printf("Студент %d: ", i);
        printf("(Победы %d, Ничьи %d, Поражения %d, Очки %d) ", 
                shared_data->results[i].wins, 
                shared_data->results[i].draws, 
                shared_data->results[i].losses,
                shared_data->results[i].total_points);
        printf("\n");

        // Находим максимальное количество очков
        if (shared_data->results[i].total_points > max_points) {
            max_points = shared_data->results[i].total_points;
        }
    }

    // Выводим победителей
    printf("\n Победители:\n");
    for (int i = 0; i < num_students; i++) {
        if (shared_data->results[i].total_points == max_points) {
            printf("Студент %d\n", i);
        }
    }

    // Закрываем и удаляем семафоры и разделяемую память
    sem_destroy(&sem_mutex);
    shm_unlink("/shared_memory");

    return 0;
}
