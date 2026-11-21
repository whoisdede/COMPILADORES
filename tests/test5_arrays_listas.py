# Teste 5: Arrays nativos JVM ([0]*n, array(...)) e Listas dinamicas (ArrayList)

# 5.1 - Array nativo de inteiros (equivalente ao exemplo 6.4 do relatorio)
x = [0] * 3
x[0] = 10
x[1] = 20
x[2] = 30

soma = 0
y = 3
while y > 0:
    y = y - 1
    v = x[y]
    print(v)
    soma = soma + x[y]
print(soma)
print(x)

# 5.2 - Array nativo de reais e de strings
reais = array(float, [1.5, 2.5, 3.5])
soma_r = 0.0
for r in reais:
    soma_r = soma_r + r
print(soma_r)
print(len(reais))

palavras = ["oi"] * 2
palavras[1] = "jvm"
print(palavras)

# 5.3 - Lista dinamica (ArrayList): append, pop, indexacao, len, for
lista = [100, 200]
lista.append(300)
lista[1] = 250
print(lista)
print(len(lista))
removido = lista.pop()
print(removido)

# Lista inicialmente vazia com tipo inferido via append
nomes = []
nomes.append("Ana")
nomes.append("Bruno")
for nome in nomes:
    print(nome)

# 5.4 - Passagem de array e lista como parametros de funcoes
def somar_elementos(colecao):
    total = 0
    for item in colecao:
        total = total + item
    return total

def adicionar_dobro(lst, val):
    lst.append(val * 2)

print(somar_elementos(x))
adicionar_dobro(lista, 50)
print(lista)
