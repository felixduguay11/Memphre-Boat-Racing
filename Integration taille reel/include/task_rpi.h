#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"
#include "task_sonar.h"
#include "task_height_control.h"
#include "task_xsens.h"

// Tache affichage
void Task_RPi(void *ptr);
