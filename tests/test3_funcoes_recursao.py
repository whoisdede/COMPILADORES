# Teste 3: Funcoes, procedimentos (void) e recursao (fatorial e fibonacci)
def fat(num):
    if num == 1:
        return 1
    else:
        x = num * fat(num - 1)
        return x

def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

def media(a, b):
    return (a + b) / 2.0

def mostrar_cabecalho(titulo):
    print(titulo)

mostrar_cabecalho("=== Funcoes e Recursao ===")
x = 5
a = fat(x)
print(a)
print(fib(7))
m = media(7, 8.5)
print(m)

# Descarte de retorno (pop / pop2)
fat(3)
media(1, 2.0)
print("fim")
