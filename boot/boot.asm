[org + 0x7c00]
jmp start

my_string: db "Enter your choice: ", 0
msg_1: db "First argv: ", 0
msg_2: db "Second argv: ", 0

print_string:
    mov ah, 0x0e
    int 0x10
    inc bx
    mov al, [bx]
    cmp al, 0
    jne print_string
    ret

getInput:
    mov ah, 0
    int 0x16
    cmp al, 0dh
    je FinishInput

    mov ah, 0xe
    int 0x10

    sub al, 0x30
    mov ah, 0
    push ax
    inc cx
    jmp getInput

FinishInput:
    pop ax
    dec cx
    cmp cx, 0
    jne FinishInput
    ret

Endline:
    mov al, 0xd          ;end line and return head of the line
    int 0x10
    mov al, 0xa
    int 0x10
    ret

Addition:
    mov bx, msg_1        ;get the first num
    mov al, [bx]
    call print_string
    xor cx, cx           ;use to count number of digits
    call getInput
    call Endline

    mov bx, msg_2        ;get the second num
    mov al, [bx]
    call print_string
    xor cx, cx
    call getInput

    ;sth to add digit by digit

    ret

Subtraction:

start:
    mov bx, my_string
    mov al, [bx]
    call print_string

    mov ah, 0                  ;input one char and represent 
    int 0x16
    mov ah, 0xe
    int 0x10
    call Endline
    
    call Addition

end:
    times 510 - ($ - $$) db 0
    db 0x55, 0xaa  
