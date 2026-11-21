# Teste 4: Strings (concatenacao, comparacao, len, indexacao, metodos)
def saudar(nome):
    return "Ola, " + nome + "!"

s1 = "Compiladores"
s2 = "Python"
frase = s1 + " - " + s2
print(frase)
print(len(frase))
print(s2[0], s2[5])
print(s2.upper())
print(s2.lower())

if s2 == "Python" and s1 != s2:
    print("comparacao ok")

msg = saudar("UDESC")
print(msg)
print("Numero: " + str(2026))
