import re,sys,glob
import os; ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
ES=open(f'{ROOT}/languages/es.txt',encoding='utf-8').read().split('\n')
out=list(ES)
CODE=re.compile(r'\{[^}]*\}')
problems=[]
# 1) nomes por id
names={}
for f in sorted(glob.glob(os.path.join(ROOT,'tools/ps2/pt_br/')+'names_pt*.tsv')):
    for l in open(f,encoding='utf-8'):
        l=l.rstrip('\n')
        if l: k,v=l.split('\t'); names[k]=v
places={
 '8350':'{f8 02 1C}Metrópole{end}','8351':'Santuário Bosque{end}','8352':'Sant. do Deserto{end}',
 '8353':'{f8 02 0C}Vale dos Reis{end}','8354':'Santuário do Mar{end}','8355':'Santuário Glória{end}',
 '8356':'{f8 02 08}Sant. Montanha{end}','8357':'Ruínas Proibidas{end}','8358':'{f8 02 08}Sant. Pradaria{end}',
 '8359':'{f8 02 04}Santuário Vasto{end}','835A':'{f8 02 04}Praça da Cidade{end}','835B':'{f8 02 1C}Santuário{end}',
 '835E':'Palácio do Faraó{end}','835F':'{f8 02 14}Loja Oculta{end}'}
bank=None;cur=None
for n,l in enumerate(ES):
    if l.startswith('@bank'): bank=l.split()[1];cur=None;continue
    m=re.match(r'^\[([0-9A-F]{4})\]',l)
    if m: cur=m.group(1);continue
    if l.startswith('{:'): continue
    if bank!='names' or not cur: continue
    if cur=='835C':
        if l.startswith('{if'): out[n]='{if 0047 L8F21}{f8 02 08}Campo de Duelo{end}'
        elif 'duelo' in l: out[n]='Campo Duelo Ant.{end}'
        continue
    if cur=='835D':
        if l.startswith('{if'): out[n]='{if 0047 L8F48}{f8 02 08}Loja de Cartas{end}'
        elif 'Tienda' in l: out[n]='Loja Cartas Ant.{end}'
        continue
    if cur in places and l.endswith('{end}'): out[n]=places[cur]; continue
    if cur in names and l.endswith('{end}') and re.search(r'[A-Za-zÀ-ÿ]',CODE.sub('',l)):
        lead=re.match(r'^((?:\{[^}]*\})*)',l).group(1)
        trail=re.search(r'((?:\{[^}]*\})*)$',l).group(1)
        out[n]=lead+names[cur]+trail
# 2) linhas traduzidas por numero (dialog/descriptions): arquivos lines_*.tsv "N<TAB>linha"
for f in sorted(glob.glob(os.path.join(ROOT,'tools/ps2/pt_br/')+'lines_*.tsv')):
    for raw in open(f,encoding='utf-8'):
        raw=raw.rstrip('\n')
        if not raw or '\t' not in raw: continue
        k,v=raw.split('\t',1); n=int(k)
        a=CODE.findall(ES[n]); b=CODE.findall(v)
        if a!=b: problems.append(f'{f}:{n}: codigos diferentes\n  ES {ES[n]}\n  PT {v}')
        out[n]=v
out[0]='# Yu-Gi-Oh! Forbidden Memories em portugues (BR), port de PS2 (Retrogames).'
out[1]='# Feito a partir do texto espanhol do disco PAL (mesmos codigos e caixas), ids da versao US.'
open(f'{ROOT}/languages/pt-br.txt','w',encoding='utf-8').write('\n'.join(out))
# restos em espanhol
left=0
for n,(a,b) in enumerate(zip(ES,out)):
    t=CODE.sub('',a)
    if a==b and re.search(r'[A-Za-zÀ-ÿ]{3}',t) and not a.startswith(('#','@','[','{:')): left+=1
print('nomes',len(names),'linhas sem traduzir',left,'problemas',len(problems))
for p in problems[:30]: print(p)
# aviso de linhas longas (dialogo): pedaco visivel entre {page}
def segs(l):
    return [len(re.sub(r'\{[^}]*\}','',p)) for p in l.split('{page}')]
long=[]
for f in sorted(glob.glob(os.path.join(ROOT,'tools/ps2/pt_br/')+'lines_d*.tsv')):
    for raw in open(f,encoding='utf-8'):
        raw=raw.rstrip('\n')
        if not raw: continue
        k,v=raw.split('\t',1); n=int(k)
        a=max(segs(ES[n])); b=max(segs(v))
        if b>34 and b>a: long.append((n,a,b,v))
print('linhas longas',len(long))
for x in long[:40]: print(x)
# descricoes: entradas inteiras, reflow em 20 colunas, ate 8 linhas
import textwrap
DESC={}
for f in sorted(glob.glob(os.path.join(ROOT,'tools/ps2/pt_br/')+'desc_pt*.tsv')):
    for raw in open(f,encoding='utf-8'):
        raw=raw.rstrip('\n')
        if raw and '\t' in raw: k,v=raw.split('\t',1); DESC[k]=v.strip()
s=out.index('@bank descriptions'); e=out.index('@bank names')
newdesc=[]; cur=None; toolong=[]
i=s
block=out[s:e]
res=[]; j=0
while j<len(block):
    l=block[j]; res.append(l)
    m=re.match(r'^\[([0-9A-F]{4})\]$',l)
    if m and m.group(1) in DESC:
        k=m.group(1); j+=1
        while j<len(block) and block[j] and not block[j].startswith('['): j+=1
        lines=textwrap.wrap(DESC[k],20,break_long_words=False,break_on_hyphens=False)
        if len(lines)>8 or any(len(x)>20 for x in lines): toolong.append((k,len(lines),DESC[k]))
        lines[-1]+='{end}'
        res.extend(lines)
        continue
    j+=1
out=out[:s]+res+out[e:]
open(f'{ROOT}/languages/pt-br.txt','w',encoding='utf-8').write('\n'.join(out))
print('descricoes',len(DESC),'longas demais',len(toolong))
for t in toolong: print(t)
