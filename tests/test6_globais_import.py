# Teste 6: Variaveis globais, arrays globais e import math (equivalente ao exemplo 6.5)
import math

x = 0
arr = [0] * 5
contador = 0

def pow_int(base, expoente):
    a = base
    while expoente > 1:
        base = base * a
        expoente = expoente - 1
    print(base)
    return base

def dobro(num):
    return num * 2

def incrementar():
    global contador
    contador = contador + 1

x = 3
potencia = pow_int(5, x)
dobrar = dobro(x)
print(dobrar)

arr[x] = 12345
acesso = arr[x]
print(acesso)

incrementar()
incrementar()
print(contador)

# Uso do modulo math
print(math.sqrt(16))
print(math.pow(2, 3))
print(math.floor(3.9))
