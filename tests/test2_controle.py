# Teste 2: Condicionais (if/elif/else), operadores logicos, while e for
a = 4
b = 5
c = 6

if b < a:
    if b < c:
        menor = b
    else:
        menor = c
else:
    menor = a

while menor > 0:
    print(menor)
    menor = menor - 1

# Operadores logicos (and, or, not) e elif
nota = 7.5
if nota >= 9.0:
    print("A")
elif nota >= 7.0 and nota < 9.0:
    print("B")
else:
    print("C")

if not (a == b) and (c > b or a > 100):
    print("logica ok")

# Laco for com range(n), range(ini, fim) e range(ini, fim, passo)
soma = 0
for i in range(1, 6):
    soma = soma + i
print(soma)

for j in range(6, 0, -2):
    print(j)
