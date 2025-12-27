import os
import matplotlib.pyplot as plt
from collections import Counter
import math
import numpy as np

def main():
    dir_path = "./prepared_texts"
    
    # Проверяем существование директории
    if not os.path.exists(dir_path):
        print(f"Директория {dir_path} не найдена!")
        return
    
    all_tokens_counter = Counter()
    
    # Читаем все файлы
    for filename in sorted(os.listdir(dir_path)):
        if filename.endswith(".txt"):
            file_path = os.path.join(dir_path, filename)
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read().strip()
                    if content:
                        tokens = content.split()
                        all_tokens_counter.update(tokens)
            except Exception as e:
                print(f"Ошибка при чтении файла {filename}: {e}")
    
    if not all_tokens_counter:
        print("Нет токенов для анализа!")
        return
    
    sorted_frequencies = sorted(all_tokens_counter.values(), reverse=True)
    
    # Создаем массивы для рангов и частот
    ranks = list(range(1, len(sorted_frequencies) + 1))
    frequencies = sorted_frequencies
    
    # Теоретическая кривая Ципфа: f = C / r
    # C берем как частоту самого частого слова
    C = frequencies[0]
    zipf_frequencies = [C / r for r in ranks]
    
    plt.figure(figsize=(10, 6))
    
    # Эмпирические данные (синие точки)
    plt.scatter(ranks, frequencies, s=10, alpha=0.6, label='Эмпирические данные', color='blue')
    
    # Теоретическая кривая Ципфа (красная линия)
    plt.plot(ranks, zipf_frequencies, 'r-', linewidth=2, label='Закон Ципфа (f = C/r)')
    
    plt.xscale('log')
    plt.yscale('log')
    plt.xlabel('Ранг (log scale)')
    plt.ylabel('Частота (log scale)')
    plt.title('Распределение частот токенов по закону Ципфа')
    plt.grid(True, which="both", ls="-", alpha=0.3)
    plt.legend()
    
    plt.savefig('zipf_law.png', dpi=300, bbox_inches='tight')
    plt.show()
    
    print("=" * 50)
    print(f"Проанализировано файлов: {len([f for f in os.listdir(dir_path) if f.endswith('.txt')])}")
    print(f"Общее количество токенов: {sum(sorted_frequencies)}")
    print(f"Уникальных токенов: {len(sorted_frequencies)}")
    print(f"Самый частый токен встречается: {sorted_frequencies[0]} раз")
    print(f"График сохранен как 'zipf_law.png'")
    print("=" * 50)

if __name__ == "__main__":
    main()