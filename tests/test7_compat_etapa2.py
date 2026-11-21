# Teste 7: Compatibilidade com teste_completo.py da Etapa 2
a = 5
b = 10.5
mensagem = "Compiladores etapa 2"

def soma(x, y):
    resultado = x + y
    return resultado

def verificar(valor):
    if valor >= 10:
        print(valor)
    else:
        print(0)

c = a + b * 2 - (4 / 2)

while a > 0:
    a = a - 1
    print(a)

for i in a:
    print(i)

soma(a, c)
verificar(c)
print(mensagem)
