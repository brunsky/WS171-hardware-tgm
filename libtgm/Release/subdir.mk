################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../._tgm.cpp \
../._tgm_event.cpp \
../tgm.cpp \
../tgm_event.cpp 

OBJS += \
./._tgm.o \
./._tgm_event.o \
./tgm.o \
./tgm_event.o 

CPP_DEPS += \
./._tgm.d \
./._tgm_event.d \
./tgm.d \
./tgm_event.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


