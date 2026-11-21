# Teste 1: Operadores aritmeticos, precedencia, inteiros e reais (double)
x = 5
y = 4
z = y
k = z + 5 * x / 10
print(k)

# Divisao inteira, modulo e unario
q = 17 // 3
r = 17 % 3
neg = -k
print(q, r, neg)

# Numeros reais e promocao automatica int -> double
a = 10.5
b = 2
res = a * b + (x - 1) / 2
print(res)

f = float(x) / 2.0
print(f)
print(int(f))
