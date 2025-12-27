from pymongo import MongoClient
from bs4 import BeautifulSoup
import re
import json

def connect_to_mongodb():
    client = MongoClient('localhost', 27017,
                         username='admin',
                         password='password123',
                         authSource='admin')
    
    db = client['documents_db']
    collection = db['documents']
    return collection

def extract_clean_text(html_content, source):
    if not html_content:
        return ""
    
    soup = BeautifulSoup(html_content, 'html.parser')
    
    for element in soup(["script", "style", "header", "footer", 
                         "nav", "aside", "iframe", "noscript",
                         "form", "button", "input", "select"]):
        element.decompose()
    
    if source == "stopgame.ru":
        # Удаление рекламных блоков
        ad_classes = ["adfox", "banner", "teaser", "advert", 
                     "branding", "recommendations", "comments"]
        for ad_class in ad_classes:
            for div in soup.find_all(class_=re.compile(ad_class)):
                div.decompose()
        
        # Удаление социальных виджетов
        for div in soup.find_all("div", class_="social"):
            div.decompose()
        
        # Поиск основного контента
        main_content = soup.find("article")
        if not main_content:
            main_content = soup.find("div", class_=re.compile(r"article|content|post|review"))
        
        if main_content:
            text = main_content.get_text(separator="\n", strip=True)
        else:
            text = soup.body.get_text(separator="\n", strip=True) if soup.body else ""
    
    elif source == "playground.ru":
        for div in soup.find_all(class_=re.compile(r"ad|banner|sidebar|widget")):
            div.decompose()
        
        # Поиск контента
        main_content = soup.find("div", class_=re.compile(r"content|article|post|text"))
        if main_content:
            text = main_content.get_text(separator="\n", strip=True)
        else:
            text = soup.get_text(separator="\n", strip=True)
    
    else:
        text = soup.get_text(separator="\n", strip=True)
    
    clean_text = re.sub(r'\s*\n\s*', '\n', text)
    clean_text = re.sub(r'\n{3,}', '\n\n', clean_text)
    
    return clean_text.strip()

def collect_corpus_statistics(collection):
    total_raw_size = 0
    total_clean_text_size = 0
    total_docs = 0
    processed_count = 0
    
    stopgame_count = 0
    playground_count = 0
    
    raw_sizes = []
    clean_sizes = []

    for doc in collection.find():
        total_docs += 1
        
        raw_html = doc.get('html', '')
        if raw_html:
            raw_size = len(raw_html.encode('utf-8'))
            total_raw_size += raw_size
            raw_sizes.append(raw_size)
        else:
            raw_size = 0
        
        source = doc.get('source_name', 'unknown')
        clean_text = extract_clean_text(raw_html, source)
        
        if clean_text:
            clean_size = len(clean_text.encode('utf-8'))
            total_clean_text_size += clean_size
            clean_sizes.append(clean_size)
        else:
            clean_size = 0
        
        source_name = doc.get('source_name', '')
        if 'stopgame' in source_name.lower():
            stopgame_count += 1
        elif 'playground' in source_name.lower():
            playground_count += 1
        
        processed_count += 1
        if processed_count % 10 == 0:
            print(f"Прогресс: {processed_count}")
    
    avg_raw_size = total_raw_size / total_docs if total_docs > 0 else 0
    avg_clean_size = total_clean_text_size / total_docs if total_docs > 0 else 0
    
    if raw_sizes:
        min_raw = min(raw_sizes)
        max_raw = max(raw_sizes)
        min_clean = min(clean_sizes) if clean_sizes else 0
        max_clean = max(clean_sizes) if clean_sizes else 0
    else:
        min_raw = max_raw = min_clean = max_clean = 0
    
    stats = {
        "total_documents": total_docs,
        "documents_from_stopgame": stopgame_count,
        "documents_from_playground": playground_count,
        "total_raw_size_mb": total_raw_size / (1024 * 1024),
        "total_clean_text_size_mb": total_clean_text_size / (1024 * 1024),
        "avg_doc_raw_size_mb": avg_raw_size / (1024 * 1024),
        "avg_doc_clean_size_kb": avg_clean_size / 1024,
        "min_doc_raw_size_kb": min_raw / 1024,
        "max_doc_raw_size_mb": max_raw / (1024 * 1024),
        "min_doc_clean_size_kb": min_clean / 1024,
        "max_doc_clean_size_kb": max_clean / 1024,
    }
    
    return stats

def display_statistics_table(stats):
    print(f"{'Общее количество документов':<40} {stats['total_documents']:>20}")
    print(f"{'Документов из StopGame.ru':<40} {stats['documents_from_stopgame']:>20}")
    print(f"{'Документов из PlayGround.ru':<40} {stats['documents_from_playground']:>20}")
    print("")
    print(f"{'Размер "сырых" HTML-документов':<40} {stats['total_raw_size_mb']:>19.2f} МБ")
    print(f"{'Размер очищенного текста':<40} {stats['total_clean_text_size_mb']:>19.2f} МБ")
    print("")
    print(f"{'Средний размер HTML-документа':<40} {stats['avg_doc_raw_size_mb']:>19.2f} МБ")
    print(f"{'Средний объем текста в документе':<40} {stats['avg_doc_clean_size_kb']:>19.2f} КБ")
    print("")
    print(f"{'Минимальный размер HTML':<40} {stats['min_doc_raw_size_kb']:>19.2f} КБ")
    print(f"{'Максимальный размер HTML':<40} {stats['max_doc_raw_size_mb']:>19.2f} МБ")
    print(f"{'Минимальный объем текста':<40} {stats['min_doc_clean_size_kb']:>19.2f} КБ")
    print(f"{'Максимальный объем текста':<40} {stats['max_doc_clean_size_kb']:>19.2f} КБ")

def main():
    try:        
        # 1. Подключение к MongoDB
        collection = connect_to_mongodb()
        
        # 2. Сбор статистики
        stats = collect_corpus_statistics(collection)
        
        # 3. Вывод статистики
        display_statistics_table(stats)
        

        print(f"Количество документов: {stats['total_documents']}")
        print(f"Общий размер HTML: {stats['total_raw_size_mb']:.2f} МБ")
        print(f"Общий размер текста: {stats['total_clean_text_size_mb']:.2f} МБ")
        print(f"Средний размер документа: {stats['avg_doc_raw_size_mb']:.2f} МБ")
        print(f"Средний объем текста: {stats['avg_doc_clean_size_kb']:.2f} КБ")
        
    except Exception as e:
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()