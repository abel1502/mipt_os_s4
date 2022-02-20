# Функция `panic` (1 балл)
В HeLL OS есть макросы `BUG_ON` и `BUG_ON_NULL` (реализация в [panic.h](panic.h)). Их можно (и нужно!) использовать в случаях, если вы ожидаете какие-то инварианты кода. Например, если ожидается, что какой-то указатель всегда ненулевой, стоит написать перед этим `BUG_ON_NULL(ptr)`. Если переданный им предикат не выполняются, эти функции аварийно завершат ядро. Однако, что такое аварийное завершение ядра? Мы не можем просто отключить питание — тогда пользователь не увидит сообщение об ошибке. Поэтому ядру нужно просто "зависнуть" на одном месте. За это должна отвечать функция `__panic`: она принимает на вход уже форматированное сообщение об ошибке, которое должна выести на экран, а затем зависнуть в бесконечном цикле. В этом задании вам предлагается реализовать эту функцию в файле [panic.c](panic.c). Бесконечный цикл должен быть организован также, как в конце `boot.S` через инструкцию `hlt`. Для этого вам понадобится [inline assembly](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html).

# Парсинг регионов памяти из Multiboot2 (1 балл)
После загрузки ядра bootloader'ом, процессор оказывается в самом начале функции `_start` (в [boot.S](boot.S)). Он имеет определённое состоние, которое описано в [спецификации Multiboot2](https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html), секция "I386 machine state". Например, `eax` всегда содержит специальное магическое значение `0x36D76289`, а `ebx` содержит указатель на структуру, которая сгенерированная загрузчиком. В этой структуре есть довольно много всякой полезной информации, однако, нас пока будет интересовать информация о доступной памяти. Цель этого задания — прочитать документацию и реализовать read-only итератор, который позволяет проходить по всем регионам памяти. Интерфейс итератора можно найти в файле [multiboot.h](multiboot.h), пример использования — в [kernel.c](kernel.c). Обратите внимание на комментарий перед `multiboot_init` – эта функция должна паниковать, если загрузчиком не предоставлена информация о регионах памяти.

# Скроллинг терминала (1 балл)
Если сейчас вы попытаетесь вывести больше 25 строчек c помощью `printk`, то самые верхние начнут перезатираться. Это неудобно и нечитаемо. Поэтому в этом задании вам предлагается добавить возможность скроллить строки терминала: то есть, если появляется новая строчка, самая первая строчка "уезжает наверх", все строчки потягиваются на одну вверх, а новая занимает место последней. Пролистывание мышкой/клавишами пока реализовывать не нужно: для этого нужны прерывания, мы пока не умеем пользоваться ими. Функции для изменения находятся в файле [vga.c](vga.c).